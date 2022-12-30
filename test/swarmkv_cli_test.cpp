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
#define	CMD_EXEC_TIMEOUT_MS	1000*2

#include <arpa/inet.h>
struct swarmkv_cli_node_addr
{
	char ip_addr[INET6_ADDRSTRLEN];
	unsigned int p2p_port;
};

void wait_for_check()
{
	usleep(2*1000*1000);
}

struct swarmkv_cli_system
{
	int attached;
	char *cluster_name;
	char *attach_target_line;
};

static struct swarmkv_cli_system g_cli_system;

void swarmkv_cli_set_db(const char *cluster_name)
{
	if(g_cli_system.cluster_name) free(g_cli_system.cluster_name);
	asprintf(&(g_cli_system.cluster_name), "%s", cluster_name);
}

void swarmkv_cli_attach(const char *ip_addr, unsigned int p2p_port)
{
	g_cli_system.attached=1;
	if(g_cli_system.attach_target_line)
	{
		free(g_cli_system.attach_target_line);
	}
	asprintf(&(g_cli_system.attach_target_line), "--attach %s:%u", ip_addr, p2p_port);
}

void swarmkv_cli_detach()
{
	g_cli_system.attached=0;
}

typedef void swarmkv_cli_reply_funt_t(struct cmd_exec_arg* reply_arg, const char *);

int swarmkv_cli_system_cmd(struct cmd_exec_arg* reply_arg, char *result, size_t result_len, swarmkv_cli_reply_funt_t *reply_funt_t, const char *format, ...)
{
	int line_num=0;
    char command[1024] = {0};
	char *cmd_str=NULL;

    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);

	if(g_cli_system.attached)
	{
		snprintf(command, sizeof(command), "../tools/swarmkv-cli -n %s %s --exec %s", g_cli_system.cluster_name, g_cli_system.attach_target_line, cmd_str);
	}
	else
	{
		snprintf(command, sizeof(command), "../tools/swarmkv-cli -n %s --exec %s", g_cli_system.cluster_name, cmd_str);
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
			if(line_num)
			{
				p += snprintf(p, result_len - (p - result), ",");
			}

			p += snprintf(p, result_len - (p - result), "%s", line);
		}
		line_num++;
    }
    pclose(fp);

	if(reply_funt_t)
		reply_funt_t(reply_arg, result);
	free(cmd_str);
	return 1;
}

void swarmkv_expect_reply_string(struct cmd_exec_arg* reply_arg, const char *line)
{
	EXPECT_STREQ(line, reply_arg->expected_reply.str);
}

void swarmkv_expect_reply_integer(struct cmd_exec_arg* reply_arg, const char *line)
{
	char reply_str[256]={0};
	snprintf(reply_str, sizeof(reply_str), "%s %llu", "(integer)", reply_arg->expected_reply.integer);
	EXPECT_STREQ(line, reply_str);
}

void swarmkv_expect_reply_nil(struct cmd_exec_arg* reply_arg, const char *line)
{
	char reply_str[256]={0};
	snprintf(reply_str, sizeof(reply_str), "%s", "(nil)");
	EXPECT_STREQ(line, reply_str);
}

int swarmkv_cli_get_addr_node(char *result, struct swarmkv_cli_node_addr *node_addr)
{
	int i=0;
	char * tmp = ALLOC(char, strlen(result)+1);
	char * token = NULL, * sub_token = NULL, * saveptr = NULL;

	memcpy(tmp, result, strlen(result));

	for (token = tmp;; token = NULL)
	{
		sub_token = strtok_r(token, "\n", &saveptr);
		if (sub_token == NULL)
		{
			break;
		}

		const char *p=sub_token;
		if(0==strncasecmp(sub_token, "1) ", 3))
		{
			p+=3;
		}
		int n_read=0;
		n_read=sscanf(p, "%[^:]:%u", node_addr[i].ip_addr, &node_addr[i].p2p_port);
		if(n_read!=2)
		{
			return -1;
		}
		i++;
	}
	free(tmp);
	tmp = NULL;
	return i;
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
		swarmkv_options_set_p2p_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 6210);
		swarmkv_options_set_logger(opts, logger);
		db=swarmkv_open(opts, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s.\n", err);
			free(err);
		}
	}
	static void TearDownTestCase()
	{
		swarmkv_close(db);
		log_handle_destroy(logger);
	}
 	// Some expensive resource shared by all tests.

	static struct log_handle *logger;
	static struct swarmkv* db;
};
struct swarmkv *SwarmkvCliNodes::db;
struct log_handle *SwarmkvCliNodes::logger;

TEST_F(SwarmkvCliNodes, Basic)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_set_db("swarmkv-cli-nodes");

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "zhangsan", "BJ");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"BJ\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "get %s", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "lisi", "SH");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"SH\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "get %s", "lisi");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "xiaoming", "HN");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr[2]={0};
	cmd_exec_arg_expect_cstring(reply_arg, "1) 127.0.0.1:5210");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "KEYSPACE RLIST %s", "zhangsan");
	swarmkv_cli_get_addr_node(result, node_addr);

	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "KEYSPACE RDEL %s %s:%s", "zhangsan", "127.0.0.1", "5210");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "get %s", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	swarmkv_cli_attach("127.0.0.1", 5210);
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"zhangsan\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CRDT KEYS %s", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CRDT DEL %s", "lisi");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "CRDT KEYS %s", "lisi");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"lisi\",2) \"zhangsan\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 2);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}

/**
	SET xiaolv CQ
	CRDT DEL ip port xiaolv
	CLUSTER SANITY check
	CLUSTER SANITY heal
	GET xiaolv
*/
TEST_F(SwarmkvCliNodes, ReplicaDel)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "xiaolv", "CQ");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "KEYSPACE RLIST %s", "xiaolv");
	swarmkv_cli_get_addr_node(result, &node_addr);

	swarmkv_cli_attach(node_addr.ip_addr, node_addr.p2p_port);
	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CRDT DEL xiaolv");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"xiaolv\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "get %s", "xiaolv");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
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
TEST_F(SwarmkvCliNodes, KeyspaceRdel)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_set_db("swarmkv-cli-nodes");

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "xiaoqin", "XG");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "xiaozi", "HG");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "KEYSPACE RLIST %s", "xiaoqin");
	swarmkv_cli_get_addr_node(result, &node_addr);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "KEYSPACE RDEL %s %s:%u", "xiaoqin", node_addr.ip_addr, node_addr.p2p_port);
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "get %s", "xiaoqin");
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"xiaoqin\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"XG\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "get %s", "xiaoqin");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}

/**
	SET xiaolan SH
	KEYSPACE RADD  xioalan ip port
	KEYSPACE RLIST xiaolan
	CLUSTER SANITY check
	CLUSTER SANITY heal
	KEYSPACE RLIST xiaolan
*/
TEST_F(SwarmkvCliNodes, KeyspaceRadd)
{
	char result[2048]={0}, reply_string[128]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_set_db("swarmkv-cli-nodes");

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "xiaolan", "SH");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "KEYSPACE RLIST %s", "xiaolan");
	swarmkv_cli_get_addr_node(result, &node_addr);

	unsigned int p2p_port = (node_addr.p2p_port == 5210) ? 5211: 5210;

	sprintf(reply_string, "1) %s:%u,2) %s:%u", node_addr.ip_addr, node_addr.p2p_port, node_addr.ip_addr, p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "KEYSPACE RADD %s %s:%d", "xiaolan", node_addr.ip_addr, p2p_port);
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"xiaolan\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	sprintf(reply_string, "1) %s:%d", node_addr.ip_addr, node_addr.p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "KEYSPACE RLIST %s", "xiaolan");

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}

TEST_F(SwarmkvCliNodes, MultipleKey)
{
	int i=0;
	long long key_number=200000;
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_set_db("swarmkv-cli-nodes");
	struct swarmkv_reply *reply=NULL;
	struct swarmkv* db=SwarmkvCliNodes::db;
	for(i=0; i<key_number; i++)
	{
		reply=swarmkv_command_on(db, NULL, "SET background-key-%d background-value-%d", i, i);
		swarmkv_reply_free(reply);
	}
	printf("Set %lld cmd.\n", key_number);

	wait_for_sync();
	cmd_exec_arg_expect_integer(reply_arg, 0);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	for(i=0; i<key_number/2; i++)
	{
		reply=swarmkv_command_on(db, NULL, "KEYSPACE RDEL background-key-%d %s:%u",i, "127.0.0.1", 5210);
		swarmkv_reply_free(reply);
	}
	printf("Keyspace Rdel %lld cmd.\n", key_number/2);

	wait_for_sync();
	cmd_exec_arg_expect_integer(reply_arg, key_number/2);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	for(i=0; i<key_number/2; i++)
	{
		reply=swarmkv_command_on(db, "127.0.0.1:5210", "CRDT DEL background-key-%d",i);
		swarmkv_reply_free(reply);
	}
	printf("Replica Del %lld cmd.\n", key_number/2);

	wait_for_sync();
	cmd_exec_arg_expect_integer(reply_arg, key_number/2);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);
	cmd_exec_arg_free(reply_arg);
}

void *swarmkv_simple_node(void *args)
{
	char *command=(char *)args;
	system(command);
	free(command);
	return NULL;
}

TEST_F(SwarmkvCliNodes, DebugAssert)
{
	pthread_t pid=0;
	size_t node_number=2;
	char result[4096]={0},target[26]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	unsigned int p2p_port_start=9210;

	const char *cluster_name="swarmkv-assert";
	swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:9210");
	wait_for_sync();

	for(size_t i=node_number; i<0; i--)
	{
		char *cmd_string=ALLOC(char, 128);
		snprintf(cmd_string, 128, "../tools/simple_node -n %s -h 127.0.0.1:%zu -t 1 -k %d", cluster_name, p2p_port_start+i, 100);
		pthread_create(&pid, NULL, swarmkv_simple_node, cmd_string);
		sleep(1);
	}
	sleep(1);
	swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "CLUSTER SANITY heal");

	swarmkv_cli_set_db("swarmkv-assert");
	for(size_t i=0; i<=node_number; i++)
	{
		swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "CLUSTER ADDKEYOWNER 127.0.0.1:%zu", p2p_port_start+i);
	}
	swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "CLUSTER SLOTS");

	reply_arg=cmd_exec_arg_new();
	for(size_t i=0; i<=node_number; i++)
	{
		swarmkv_cli_attach("127.0.0.1", p2p_port_start+i);
		swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "DEBUG ASSERT");
		swarmkv_cli_detach();

		struct timeval start, end;
		gettimeofday(&start, NULL);
		while(1)
		{
			if(i==node_number) break;
			swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "CLUSTER SLOTS");
			snprintf(target, sizeof(target), "127.0.0.1:%zu", p2p_port_start+i);
			if(!strstr(result, target))
			{
				break;
			}
			gettimeofday(&end, NULL);
			if( end.tv_sec-start.tv_sec > 20)
			{
				break;
			}
		}
		swarmkv_cli_system_cmd(NULL, result, sizeof(result), NULL, "CLUSTER SANITY heal");
		cmd_exec_arg_expect_integer(reply_arg, 0);
		swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY check");
		cmd_exec_arg_clear(reply_arg);
	}
	cmd_exec_arg_free(reply_arg);
}

class SwarmkvTwoNodes : public testing::Test
{
protected:
	static void SetUpTestCase()
	{

		char *err=NULL;
		const char *log_path="./swarmkv-2-nodes.log";

		const char *cluster_name="swarmkv-2-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210 127.0.0.1:5211");

		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts1=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts1, 5210);
		swarmkv_options_set_health_check_port(opts1, 6210);
		swarmkv_options_set_logger(opts1, logger);
		db1=swarmkv_open(opts1, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}

		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts2, 5211);
		swarmkv_options_set_health_check_port(opts2, 6211);
		swarmkv_options_set_logger(opts2, logger);
		db2=swarmkv_open(opts2, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}

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

/**
	ATTACH 127.0.0.1 5210
	SET id01 111
	ATTACH 127.0.0.1 5211
	SET id01 111
	DETACH

	KEYSPACE RADD 127.0.0.1 5211
	ATTACH 127.0.0.1 5211
	CRDT PULL

	ATTACH 127.0.0.1 5210
	CRDT RLIST id01
	Reply:NULL
	ATTACH 127.0.0.1 5211
	CRDT RLIST id01
	Reply:0) 127.0.0.1:15210

	CLUSTER SANITY check
	CLUSTER SANITY heal
	KEYSPACE RLIST id01
	0) 127.0.0.1:5210
	1) 127.0.0.1:5211

	ATTACH 127.0.0.1 5210
	CRDT RLIST id01
	Reply:0) 127.0.0.1:5211
	ATTACH 127.0.0.1 5211
	CRDT RLIST id01
	Reply:0) 127.0.0.1:15210
*/
#if 0
TEST_F(SwarmkvTwoNodes, Crdt_Join)
{
	char result[2048]={0}, reply_string[64]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	const char *node_addr="127.0.0.1";
	unsigned int node1_p2p_port=5210, node2_p2p_port=5211;

	swarmkv_cli_set_db("swarmkv-2-nodes");
	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_attach(node_addr, node1_p2p_port);
	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "id01", "111");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	swarmkv_cli_attach(node_addr, node2_p2p_port);
	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "id01", "222");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	sprintf(reply_string, "1) %s:%u", node_addr, node1_p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "KEYSPACE RADD %s %s:%u", "id01", node_addr, node1_p2p_port);
	cmd_exec_arg_clear(reply_arg);

	swarmkv_cli_attach(node_addr, node2_p2p_port);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "CRDT PULL %s", "id01");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	swarmkv_cli_attach(node_addr, node1_p2p_port);
	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "CRDT RLIST id01");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	swarmkv_cli_attach(node_addr, node2_p2p_port);
	/*Because the ports are random, they cannot be compared**/
	sprintf(reply_string, "1) %s", "127.0.0.1");
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "CRDT RLIST id01");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"id01\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	sprintf(reply_string, "1) %s:%d,2) %s:%d", node_addr, node1_p2p_port, node_addr, node2_p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "KEYSPACE RLIST %s", "id01");

	swarmkv_cli_attach(node_addr, node1_p2p_port);
	sprintf(reply_string, "1) %s:%s", "127.0.0.1", "5211");
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CRDT RLIST id01");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	swarmkv_cli_attach(node_addr, node2_p2p_port);
	/*Because the ports are random, they cannot be compared**/
	sprintf(reply_string, "1) %s", "127.0.0.1");
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), NULL, "CRDT RLIST id01");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}

TEST_F(SwarmkvTwoNodes, Set_Key)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;

	reply_arg=cmd_exec_arg_new();

	swarmkv_cli_attach("127.0.0.1", 5210);
	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "set %s %s", "id001", "1");
	cmd_exec_arg_clear(reply_arg);
	swarmkv_cli_detach();

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_nil, "get %s", "id001");
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "1) \"id001\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"1\"");
	swarmkv_cli_system_cmd(reply_arg, result, sizeof(result), swarmkv_expect_reply_string, "get %s", "id001");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}
#endif
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
