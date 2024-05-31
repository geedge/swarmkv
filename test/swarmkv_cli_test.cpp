#include "swarmkv/swarmkv.h"
#include "test_utils.h"
#include "log.h"

#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/time.h>

#include <arpa/inet.h>
#include <swarmkv_tools.h> 

void wait_for_node_run()
{
	usleep(2*1000*1000);
}

class SwarmkvCliNodes : public testing::Test
{
protected:
	static void SetUpTestCase()
	{
		const char* log_path="./swarmkv-cli-nodes.log";
		char *err=NULL;
		const char *cluster_name="swarmkv-cli-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210");
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 6210);
		swarmkv_options_set_logger(opts, logger);
		swarmkv_options_set_caller_thread_number(opts, 1);
		db=swarmkv_open(opts, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s.\n", err);
			free(err);
		}
		swarmkv_register_thread(db);

		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_dryrun(opts2);
		swarmkv_options_set_cluster_port(opts2, 16211);
		swarmkv_options_set_health_check_port(opts2, 16212);
		swarmkv_options_set_logger(opts2, logger);
		swarmkv_options_set_caller_thread_number(opts2, 1);
		db2=swarmkv_open(opts2, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db2);

	}
	static void TearDownTestCase()
	{
		swarmkv_close(db);
		log_handle_destroy(logger);
	}
 	// Some expensive resource shared by all tests.

	static struct log_handle *logger;
	static struct swarmkv* db, *db2;
};
struct swarmkv *SwarmkvCliNodes::db;
struct swarmkv *SwarmkvCliNodes::db2;
struct log_handle *SwarmkvCliNodes::logger;

enum swarmkv_cli_cmd
{
	CLI_CMD_EXEC,
	CLI_CMD_CREATE_CLUSTER,
	CLI_CMD_ATTACH,
	CLI_CMD_CONSUL
};

void config_init(struct swarmkv *db, const char *cluster_name)
{
	memset(&g_config, 0, sizeof(g_config));
	g_config.consul_port=8500;
	strcpy(g_config.consul_host, "127.0.0.1");
	g_config.cluster_port=5210;
	strncpy(g_config.db_name, cluster_name, sizeof(g_config.db_name));
	g_config.db=db;
}

int swarmkv_cli_exec_cmd(const char *db, const char *ip_addr, unsigned int port, char *result, size_t result_len, enum swarmkv_cli_cmd cmd_type, const char *format, ...)
{
	int line_num=0;
	char *cmd_str=NULL;
	char command[1024] = {0};

	va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);

	switch(cmd_type)
	{
		case CLI_CMD_CREATE_CLUSTER:
			snprintf(command, sizeof(command), "../tools/swarmkv-cli --cluster-create %s %s", db, cmd_str);
			break;
		case CLI_CMD_EXEC:
			snprintf(command, sizeof(command), "../tools/swarmkv-cli -n %s --exec %s", db, cmd_str);
			break;
		case CLI_CMD_ATTACH:
			snprintf(command, sizeof(command), "../tools/swarmkv-cli -n %s --attach %s:%u --exec %s", db, ip_addr, port, cmd_str);
			break;
		case CLI_CMD_CONSUL:
			snprintf(command, sizeof(command), "../tools/swarmkv-cli -n %s -c %s", db, cmd_str);
			break;
		default:
			break;
	}
	char *p=result;
	char line[1024] = {0};
    FILE *fp = NULL;

    if((fp = popen(command, "r")) == NULL)
	{
        printf("popen error!\n");
        return 0;
    }
	memset(result, 0, result_len);
    while (fgets(line, sizeof(line), fp))
	{
		int len= strlen(line);
		line[len-1]='\0';
		if((p - result) < (int)result_len)
		{
			if(line_num) p += snprintf(p, result_len - (p - result), ",");
			p += snprintf(p, result_len - (p - result), "%s", line);
		}
		line_num++;
    }
    pclose(fp);
	free(cmd_str);
	return 1;
}

TEST_F(SwarmkvCliNodes, CLI_Command)
{
	int ret=0;
	char result[2048]={0};
	const char *cluster_name="swarmkv-cli-nodes";

	/*SwarmkvCli Command: --cluster-create*/
	ret=swarmkv_cli_exec_cmd("SwarmkvCliNodes_Basic", NULL, 0, result, sizeof(result), CLI_CMD_CREATE_CLUSTER, "%s", "127.0.0.1:6210");
	EXPECT_STREQ(result, "OK");

	/*SwarmkvCli Command: -n <cluster-name> --exec [Command]*/
	ret=swarmkv_cli_exec_cmd(cluster_name, NULL, 0, result, sizeof(result), CLI_CMD_EXEC, "set %s %s", "set-cmd-key", "1");
	EXPECT_STREQ(result, "OK");

	/*SwarmkvCli Command: -n <cluster-name> --attach <IP:port> --exec [Command]*/
	ret=swarmkv_cli_exec_cmd(cluster_name, "127.0.0.1", 5210, result, sizeof(result), CLI_CMD_ATTACH, "set %s %s", "set-attach-cmd-key", "1");
	EXPECT_STREQ(result, "OK");

	/*SwarmkvCli Command: -c <consul-ip:consul-port>*/
	ret=swarmkv_cli_exec_cmd(cluster_name,  NULL, 0, result, sizeof(result), CLI_CMD_CONSUL, "%s", "127.0.0.1:8501");
	EXPECT_STREQ(result, "Open DB `swarmkv-cli-nodes` error: key slots init failed.");
	ret=ret;
}

TEST_F(SwarmkvCliNodes, CLI_Get_Slots)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";
	config_init(db, cluster_name); 
	reply = cluster_slots_command(db, NULL, 0);
	EXPECT_STREQ(reply->str, "[{\n\t\t\"slot\":\t\"0-16383\",\n\t\t\"owner\":\t\"127.0.0.1:5210\"\n\t}]");
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvCliNodes, CLI_Get_Nodes)
{
	struct swarmkv *db=SwarmkvCliNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";

	config_init(db, cluster_name); 
	wait_for_node_run();
	reply = cluster_nodes_command(db, NULL, 0);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_TRUE(strstr(reply->elements[0]->str, "127.0.0.1:5210"));
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvCliNodes, SANITY_BASIC)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	const char *cluster_name="swarmkv-cli-nodes";

	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_command(db, "SET %s %s", "zhangsan", "BJ");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SET %s %s", "lisi", "SH");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SET %s %s", "xiaoming", "HN");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "KEYSPACE RDEL %s %s:%s", "zhangsan", "127.0.0.1", "5210");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
	reply=NULL;

	reply=swarmkv_command(db, "CRDT DEL %s", "lisi");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "CRDT KEYS %d %s", 0, "lisi");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	config_init(db, cluster_name); 
	wait_for_node_run();
	const char *argv[3];size_t argc=3;
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="check";
	reply=cluster_sanity_command(db, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 2);
	EXPECT_STREQ(reply->elements[0]->str, "lisi");
	EXPECT_STREQ(reply->elements[1]->str, "zhangsan");
	swarmkv_reply_free(reply);

	memset(argv, 0, sizeof(argv)*argc);
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	reply=cluster_sanity_command(db, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 2);
	swarmkv_reply_free(reply);
}

/**
	SET xiaolv CQ
	CRDT DEL ip port xiaolv
	CLUSTER SANITY check
	CLUSTER SANITY heal
	GET xiaolv
*/
TEST_F(SwarmkvCliNodes, SANITY_CRDT_DEL)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv *db2=SwarmkvCliNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";

	reply=swarmkv_command(db2, "SET %s %s", "xiaolv", "CQ");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "CRDT DEL %s", "xiaolv");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "CRDT EXISTS %s", "xiaolv");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	config_init(db, cluster_name); 
	wait_for_node_run();
	const char *argv[3];size_t argc=3;
	memset(argv, 0, sizeof(argv)*argc);
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	reply=cluster_sanity_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "get %s", "xiaolv");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);
}

/**
	SET xiaoqin XG
	SET xiaozi HG
	KEYSPACE RDEL ip port xiaoqin
	GET xiaoqin
	CLUSTER SANITY check
	CLUSTER SANITY heal
	GET xiaoqin
*/
TEST_F(SwarmkvCliNodes, SANITY_KEYSPACE_RDEL)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv *db2=SwarmkvCliNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";

	reply=swarmkv_command(db2, "SET %s %s", "xiaoqin", "XG");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "KEYSPACE RDEL %s %s:%s", "xiaoqin", "127.0.0.1", "5210");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "get %s", "xiaoqin");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	wait_for_node_run();
	config_init(db, cluster_name); 
	const char *argv[3];size_t argc=3;
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	reply=cluster_sanity_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "get %s", "xiaoqin");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "XG");
	swarmkv_reply_free(reply);
}

/**
	SET xiaolan SH
	KEYSPACE RADD  xioalan ip port
	KEYSPACE RLIST xiaolan
	CLUSTER SANITY check
	CLUSTER SANITY heal
	KEYSPACE RLIST xiaolan
*/
TEST_F(SwarmkvCliNodes, SANITY_KEYSPACE_RADD)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv *db2=SwarmkvCliNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";

	reply=swarmkv_command(db2, "SET %s %s", "xiaolan", "SH");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "KEYSPACE RADD %s %s:%s", "xiaolan", "127.0.0.1", "5211");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 2);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:5210");
	EXPECT_STREQ(reply->elements[1]->str, "127.0.0.1:5211");
	swarmkv_reply_free(reply);

	wait_for_node_run();
	config_init(db, cluster_name); 
	const char *argv[3];size_t argc=3;
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	reply=cluster_sanity_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "KEYSPACE RLIST %s", "xiaolan");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 1);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:5210");
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvCliNodes, Performance)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-cli-nodes";
	struct timeval now, end;
	
	int i=0;
	long long key_number=200000;

	gettimeofday(&now, NULL);
	for(i=0; i<key_number; i++)
	{
		reply=swarmkv_command(db, "SET background-key-%d background-value-%d", i, i);
		swarmkv_reply_free(reply);
	}
	gettimeofday(&end, NULL);
	printf("Set %lld cmd elapsed time %ld.\n", key_number, (end.tv_sec - now.tv_sec));

	config_init(db, cluster_name); 
	wait_for_node_run();

	const char *argv[3];size_t argc=3;
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="check";
	gettimeofday(&now, NULL);
	reply=cluster_sanity_command(db, (char **)argv, argc);
	gettimeofday(&end, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);
	printf("Cluster Sanity Check elapsed time %ld.\n", (end.tv_sec - now.tv_sec));

	gettimeofday(&now, NULL);
	for(i=0; i<key_number/2; i++)
	{
		reply=swarmkv_command(db, "KEYSPACE RDEL background-key-%d %s:%u",i, "127.0.0.1", 5210);
		swarmkv_reply_free(reply);
	}
	gettimeofday(&end, NULL);
	printf("Keyspace Rdel %lld cmd elapsed time %ld.\n", key_number/2, (end.tv_sec - now.tv_sec));

	memset(argv, 0, sizeof(argv)*argc);
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	gettimeofday(&now, NULL);
	reply=cluster_sanity_command(db, (char **)argv, argc);
	gettimeofday(&end, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, key_number/2);
	swarmkv_reply_free(reply);
	printf("Cluster Sanity Heal  %lld cmd elapsed time %ld.\n", reply->integer, (end.tv_sec - now.tv_sec));

	gettimeofday(&now, NULL);
	for(i=0; i<key_number/2; i++)
	{
		reply=swarmkv_command(db, "CRDT DEL background-key-%d",i);
		swarmkv_reply_free(reply);
	}
	gettimeofday(&end, NULL);
	printf("Replica Del %lld cmd elapsed time %ld.\n", key_number/2, (end.tv_sec - now.tv_sec));

	gettimeofday(&now, NULL);
	reply=cluster_sanity_command(db, (char **)argv, argc);
	gettimeofday(&end, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, key_number/2);
	swarmkv_reply_free(reply);
	printf("Cluster Sanity Heal %lld cmd elapsed time %ld.\n", reply->integer, (end.tv_sec - now.tv_sec));
}

void *pthread_run_simple_node(void *args)
{
	int id = (int)(uintptr_t)args;
	char command[1024] = {0};
	unsigned int p2p_port_start=9210;
	const char *cluster_name="swarmkv-anomaly-node";	
	snprintf(command, 1024, "../tools/simple_node -n %s -h 127.0.0.1:%u -t 1 -k %d --exec as node pod", cluster_name, p2p_port_start+id, 100);
	system(command);
	return NULL;
}

struct swarmkv *create_swarmkv_db()
{
	char *err=NULL;
	struct swarmkv *db=NULL;
	static struct log_handle *logger;
	const char* log_path="./swarmkv-anomaly-node.log";
	const char *cluster_name="swarmkv-anomaly-node";
	logger=log_handle_create(log_path, 0);
	struct swarmkv_options* opts=swarmkv_options_new();
	swarmkv_options_set_dryrun(opts);
	swarmkv_options_set_cluster_port(opts, 9215);
	swarmkv_options_set_health_check_port(opts, 10215);
	swarmkv_options_set_logger(opts, logger);
	swarmkv_options_set_caller_thread_number(opts, 1);
	db=swarmkv_open(opts, cluster_name, &err);
	if(err)
	{
		printf("swarmkv_open failed: %s.\n", err);
		free(err);
	}
	swarmkv_register_thread(db);
	return db;
}

/**
 * test for cluster_create_command
 * test for cluster_addslotowner_command
 * test for cluster_sanity_command
 * 
*/
TEST_F(SwarmkvCliNodes, MultiInterface)
{
	struct swarmkv *db=SwarmkvCliNodes::db;
	struct swarmkv *db2=SwarmkvCliNodes::db2;
	struct swarmkv_reply *reply=NULL;
	unsigned int p2p_port_start=9210;
	const char *cluster_name="swarmkv-anomaly-node";

	config_init(db, cluster_name); 
	const char *argv[2];size_t argc=2;
	argv[0]=cluster_name; argv[1]="127.0.0.1:9210";
	reply=cluster_create_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");

	pthread_t pid=0;size_t node_number=3;
	for(size_t i=0; i<node_number; i++)
	{
		pthread_create(&pid, NULL, pthread_run_simple_node, (void *)(uintptr_t)i);
	}

	sleep(1);
	char node_addr[256] = {0};
	for(size_t i=0; i<node_number; i++)
	{
		memset(node_addr, 0, sizeof(node_addr));
		memset(argv, 0, sizeof(argv)*argc);
		argc=3;
		argv[0]="CLUSTER";
	    argv[1]="ADDSLOTOWNER";
		snprintf(node_addr, 256, "127.0.0.1:%zu", p2p_port_start+i);
		argv[2]=node_addr; 
		reply=cluster_addslotowner_command(db2,(char **)argv, argc);
		sleep(1);
	}
	sleep(5);
	reply = cluster_slots_command(db2, NULL, 0);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_VERBATIM);
	EXPECT_TRUE(strstr(reply->str, "127.0.0.1:9210"));
	EXPECT_TRUE(strstr(reply->str, "127.0.0.1:9211"));
	EXPECT_TRUE(strstr(reply->str, "127.0.0.1:9212"));
	swarmkv_reply_free(reply);

	char result[2048]={0},target[26]={0};
	struct swarmkv *db3=create_swarmkv_db();
	for(size_t i=0; i<node_number; i++)
	{
		swarmkv_cli_exec_cmd(cluster_name, "127.0.0.1", p2p_port_start+i, result, sizeof(result), CLI_CMD_ATTACH, "DEBUG ASSERT");

		struct timeval start, end;
		gettimeofday(&start, NULL);
		while(1)
		{
			if(i==node_number) break;
			reply = cluster_slots_command(db2, NULL, 0);
			ASSERT_EQ(reply->type, SWARMKV_REPLY_VERBATIM);
			snprintf(target, sizeof(target), "127.0.0.1:%zu", p2p_port_start+i);
			if(!strstr(result, target) || i==2)
			{
				break;
			}
			gettimeofday(&end, NULL);
			if( end.tv_sec-start.tv_sec > 120)
			{
				break;
			}
		}
		memset(argv, 0, sizeof(argv)*argc);
		argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
		reply=cluster_sanity_command(db3, (char **)argv, argc);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		swarmkv_reply_free(reply);

		memset(argv, 0, sizeof(argv)*argc);
		argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="check";
		reply=cluster_sanity_command(db3, (char **)argv, argc);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		swarmkv_reply_free(reply);
	}
}

class SwarmkvTwoNodes : public testing::Test
{
protected:
	static void SetUpTestCase()
	{

		char *err=NULL;
		const char *log_path="./swarmkv-2-nodes.log";

		const char *cluster_name="swarmkv-2-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:10210 127.0.0.1:10211");

		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts1=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts1, 10210);
		swarmkv_options_set_health_check_port(opts1, 20210);
		swarmkv_options_set_logger(opts1, logger);
		swarmkv_options_set_caller_thread_number(opts1, 1);
		db1=swarmkv_open(opts1, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db1);

		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts2, 10211);
		swarmkv_options_set_health_check_port(opts2, 20211);
		swarmkv_options_set_logger(opts2, logger);
		swarmkv_options_set_caller_thread_number(opts2, 1);
		db2=swarmkv_open(opts2, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db2);

	}
	static void TearDownTestCase()
	{
		swarmkv_close(db1);
		swarmkv_close(db2);
		log_handle_destroy(logger);
	}
 	// Some expensive resource shared by all tests.
 	static struct log_handle *logger;
	static struct swarmkv *db1, *db2;
};
struct log_handle* SwarmkvTwoNodes::logger;
struct swarmkv* SwarmkvTwoNodes::db1;
struct swarmkv* SwarmkvTwoNodes::db2;

TEST_F(SwarmkvTwoNodes, SANITY_BASIC)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *cluster_name="swarmkv-2-nodes";

	reply=swarmkv_command_on(db1, "127.0.0.1:10210", "SET %s %s", "id01", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db2, "127.0.0.1:10211", "SET %s %s", "id01", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db1, "127.0.0.1:10211", "KEYSPACE RDEL %s %s:%s", "id01", "127.0.0.1", "10210");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
	reply=NULL;

	wait_for_node_run();
	config_init(db1, cluster_name); 
	const char *argv[3];size_t argc=3;
	argv[0]="CLUSTER"; argv[1]="SANITY";argv[2]="heal";
	reply=cluster_sanity_command(db1, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "KEYSPACE RLIST %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 2);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:10211");
	EXPECT_STREQ(reply->elements[1]->str, "127.0.0.1:10210");

	reply=swarmkv_command_on(db2, "127.0.0.1:10211", "CRDT DEL %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=cluster_sanity_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "KEYSPACE RLIST %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 1);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:10210");

	reply=swarmkv_command_on(db2, "127.0.0.1:10210", "CRDT ADD %s %s", "id01", "127.0.0.1:10211");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db1, "127.0.0.1:10210", "CRDT RLIST %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 1);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:10211");
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db2, "127.0.0.1:10211", "CRDT EXISTS %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=cluster_sanity_command(db2, (char **)argv, argc);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db2, "127.0.0.1:10211", "get %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "id01");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "KEYSPACE RLIST %s", "id01");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 2);
	EXPECT_STREQ(reply->elements[0]->str, "127.0.0.1:10210");
	EXPECT_STREQ(reply->elements[1]->str, "127.0.0.1:10211");
	swarmkv_reply_free(reply);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
