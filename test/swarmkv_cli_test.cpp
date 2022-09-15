#include "swarmkv/swarmkv.h"
#include "test_utils.h"
#include "log.h"

#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
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

typedef void swarmkv_cli_reply_funt_t(struct cmd_exec_arg* reply_arg, const char *);

int swarmkv_cli_system_cmd(struct cmd_exec_arg* reply_arg, char *result, swarmkv_cli_reply_funt_t *reply_funt_t, const char *format, ...)
{
	int line_num=0;
    char command[1024] = {0};
	char *cmd_str=NULL;

    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);
    snprintf(command, 1024, "../tools/swarmkv-cli -n swarmkv-cli-nodes --exec %s", cmd_str);

	char line[1024] = {0};
    FILE *fp = NULL;

    if((fp = popen(command, "r")) == NULL)
	{
        printf("popen error!\n");
        return 0;
    }

	memset(result, 0, 1024);
    while (fgets(line, sizeof(line), fp))
	{
		int len= strlen(line);
		line[len-1]='\0';

		if(line_num)
		{
			strcat(result, ",");
		}
		strcat(result, line);
		line_num++;
    }
    pclose(fp);

	if(reply_funt_t)
		reply_funt_t(reply_arg, result);

	printf("EXEC : %s\n", cmd_str);
	printf("Reply: %s\n", result);

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
		if(0==strncasecmp(sub_token, "0) -ASK ", 8))
		{
			p+=8;
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

		char *err=NULL;
		const char *log_path="./swarmkv-cli-nodes.log";

		const char *cluster_name="swarmkv-cli-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1 5210");

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
	}
	static void TearDownTestCase()
	{
		swarmkv_close(db1);
		log_handle_destroy(logger);
	}
 	static struct log_handle *logger;
	static struct swarmkv *db1;
};
struct log_handle* SwarmkvCliNodes::logger;
struct swarmkv* SwarmkvCliNodes::db1;

TEST_F(SwarmkvCliNodes, CLUSTER_SANITY_CHECK_NODE)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "zhangsan", "BJ");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"BJ\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "get %s", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "lisi", "SH");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"SH\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "get %s", "lisi");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "xiaoming", "HN");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr[2]={0};
	cmd_exec_arg_expect_cstring(reply_arg, "0) -ASK 127.0.0.1:5210");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "KEYSPACE RLIST %s", "zhangsan");
	swarmkv_cli_get_addr_node(result, node_addr);

	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "KEYSPACE RDEL %s %s %s", "127.0.0.1", "5210", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_nil, "get %s", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "0) \"zhangsan\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "CRDT KEYS %s %s %s", "127.0.0.1", "5210", "zhangsan");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CRDT DEL %s %s %s", "127.0.0.1", "5210", "lisi");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_nil, "CRDT KEYS %s %s %s", "127.0.0.1", "5210", "lisi");
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "0) \"lisi\",1) \"zhangsan\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 2);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
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
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1 5210 127.0.0.1 5211");

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
	SET xiaolan SH
	KEYSPACE RADD  xioalan ip port
	KEYSPACE RLIST xiaolan
	CLUSTER SANITY check
	CLUSTER SANITY heal
	KEYSPACE RLIST xiaolan
*/
TEST_F(SwarmkvCliNodes, SANITY_CHECK_KEYSPACE_ADD)
{
	char result[2048]={0}, reply_string[64]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "xiaolan", "SH");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, NULL, "KEYSPACE RLIST %s", "xiaolan");
	swarmkv_cli_get_addr_node(result, &node_addr);

	unsigned int p2p_port = (node_addr.p2p_port == 5210) ? 5211: 5210;

	sprintf(reply_string, "0) -ASK %s:%d,1) -ASK %s:%d", node_addr.ip_addr, node_addr.p2p_port, node_addr.ip_addr, p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "KEYSPACE RADD %s %s %d", "xiaolan", node_addr.ip_addr, p2p_port);
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "0) \"xiaolan\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	sprintf(reply_string, "0) -ASK %s:%d", node_addr.ip_addr, node_addr.p2p_port);
	cmd_exec_arg_expect_cstring(reply_arg, reply_string);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "KEYSPACE RLIST %s", "xiaolan");

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
TEST_F(SwarmkvCliNodes, CMD_SANITY_CHECK_CRDT_DEL)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "xiaolv", "CQ");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, NULL, "KEYSPACE RLIST %s", "xiaolv");
	swarmkv_cli_get_addr_node(result, &node_addr);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CRDT DEL %s %d %s", node_addr.ip_addr, node_addr.p2p_port, "xiaolv");
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "0) \"xiaolv\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_nil, "get %s", "xiaolv");
	cmd_exec_arg_clear(reply_arg);

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
TEST_F(SwarmkvCliNodes, CMD_SANITY_CHECK_KEYSPACE_RDEL)
{
	char result[2048]={0};
	struct cmd_exec_arg* reply_arg=NULL;
	reply_arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "xiaoqin", "XG");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_OK(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "set %s %s", "xiaozi", "HG");
	cmd_exec_arg_clear(reply_arg);

	struct swarmkv_cli_node_addr node_addr;
	swarmkv_cli_system_cmd(reply_arg, result, NULL, "KEYSPACE RLIST %s", "xiaoqin");
	swarmkv_cli_get_addr_node(result, &node_addr);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "KEYSPACE RDEL %s %d %s", node_addr.ip_addr, node_addr.p2p_port, "xiaoqin");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_NIL(reply_arg);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_nil, "get %s", "xiaoqin");
	cmd_exec_arg_clear(reply_arg);

	wait_for_check();
	cmd_exec_arg_expect_cstring(reply_arg, "0) \"xiaoqin\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "CLUSTER SANITY check");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_integer(reply_arg, 1);
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_integer, "CLUSTER SANITY heal");
	cmd_exec_arg_clear(reply_arg);

	cmd_exec_arg_expect_cstring(reply_arg, "\"XG\"");
	swarmkv_cli_system_cmd(reply_arg, result, swarmkv_expect_reply_string, "get %s", "xiaoqin");
	cmd_exec_arg_clear(reply_arg);

	wait_for_sync();
	cmd_exec_arg_free(reply_arg);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
