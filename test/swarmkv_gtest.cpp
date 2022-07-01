#include "swarmkv/swarmkv.h"
#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#define	CMD_EXEC_TIMEOUT_MS	1000*2
int swarmkv_cli_create_cluster(const char* cluster_name, const char *node_string)
{
	char cmd_string[1024];
	snprintf(cmd_string, sizeof(cmd_string), "../tools/swarmkv-cli --cluster-create %s %s", cluster_name, node_string);
	return system(cmd_string);
}
void wait_for_sync()
{
	usleep(100*1000);
}
struct cmd_exec_arg
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int is_callback_executed;
	int success;
	int api_invoking_tid;
	int callback_invoking_tid;
	int is_sync_callback;
	struct swarmkv_reply expected_reply;
};
struct cmd_exec_arg* cmd_exec_arg_new(void)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)calloc(sizeof(struct cmd_exec_arg), 1);
	pthread_cond_init(&arg->cond, NULL);
	pthread_mutex_init(&arg->mutex, NULL);
	arg->api_invoking_tid = syscall(SYS_gettid);
	return arg;
}
void cmd_exec_arg_free(struct cmd_exec_arg* arg)
{
	pthread_cond_destroy(&arg->cond);
	pthread_mutex_destroy(&arg->mutex);
	
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}

	free(arg);
}
void cmd_exec_arg_clear(struct cmd_exec_arg* arg)
{
	arg->success=0;
	arg->is_callback_executed=0;
	arg->api_invoking_tid = syscall(SYS_gettid);
	arg->callback_invoking_tid=0;
	arg->is_sync_callback=0;
	return;
}

void cmd_exec_arg_expect_cstring(struct cmd_exec_arg* arg, const char* string)
{
	arg->expected_reply.type=SWARMKV_REPLY_STRING;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup(string);
	arg->expected_reply.len=strlen(string);

}
void cmd_exec_arg_expect_integer(struct cmd_exec_arg* arg, long long integer)
{
	arg->expected_reply.type=SWARMKV_REPLY_INTEGER;
	arg->expected_reply.integer=integer;
}
void cmd_exec_arg_expect_OK(struct cmd_exec_arg* arg)
{
	arg->expected_reply.type=SWARMKV_REPLY_STATUS;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup("OK");
	arg->expected_reply.len=2;
}
void cmd_exec_arg_expect_NIL(struct cmd_exec_arg* arg)
{
	arg->expected_reply.type=SWARMKV_REPLY_NIL;
}

void error_pthread_cond_timedwait(const int timed_wait_rv)
{
    fprintf(stderr, "Conditional timed wait, failed.\n");
    switch (timed_wait_rv)
    {
        case ETIMEDOUT:
            fprintf(stderr, "The time specified by abstime to pthread_cond_timedwait() has passed.\n");
            break;
        case EINVAL:
            fprintf(stderr, "The value specified by abstime, cond or mutex is invalid.\n");
            break;
        case EPERM:
            fprintf(stderr, "The mutex was not owned by the current thread at the time of the call.\n");
            break;
        default:
            break;
    }
}

int cmd_exec_arg_wait(struct cmd_exec_arg* arg, long timeout_ms)
{
	struct timespec max_wait = {0, 0};
	clock_gettime(CLOCK_REALTIME, &max_wait);
	max_wait.tv_sec+=timeout_ms/1000;
	max_wait.tv_nsec+=(timeout_ms%1000)*1000*1000;	
	int timed_wait_rv=0;
	if(arg->is_callback_executed)
	{
		return arg->success;
	}
	pthread_mutex_lock(&arg->mutex);
	timed_wait_rv=pthread_cond_timedwait(&arg->cond, &arg->mutex, &max_wait);
	pthread_mutex_unlock(&arg->mutex);
	if(timed_wait_rv&& !(arg->is_callback_executed))
	{
		error_pthread_cond_timedwait(timed_wait_rv);
		return -1;
	}
	else
	{
		return arg->success;
	}
}

void cmd_exec_arg_success(struct cmd_exec_arg* arg)
{
	arg->is_callback_executed=1;
	arg->success=1;
	arg->callback_invoking_tid = syscall(SYS_gettid);
	if(arg->callback_invoking_tid==arg->api_invoking_tid)
	{
		arg->is_sync_callback=1;
	}
	pthread_cond_signal(&arg->cond);
}
void cmd_exec_arg_failed(struct cmd_exec_arg* arg)
{
	arg->is_callback_executed=1;
	arg->success=0;
	if(arg->callback_invoking_tid==arg->api_invoking_tid)
	{
		arg->is_sync_callback=1;
	}
	pthread_cond_signal(&arg->cond);
}


size_t swarmkv_reply_print(const struct swarmkv_reply* reply, char* buff, size_t buff_sz)
{
	size_t print_len=0;
	switch(reply->type)
	{
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
			print_len=snprintf(buff, buff_sz, "%.*s", reply->len, reply->str);
			break;
		case SWARMKV_REPLY_INTEGER:
			print_len=snprintf(buff, buff_sz, "%lld", reply->integer);
			break;
		case SWARMKV_REPLY_NIL:
			print_len=snprintf(buff, buff_sz, "null");
			break;
		case SWARMKV_REPLY_ARRAY:
			print_len=snprintf(buff, buff_sz, "array(%zu)", reply->n_element);
			break;
		default:
			assert(0);
			break;
	}
	return print_len;
}


int reply_compare(const struct swarmkv_reply *r1, const swarmkv_reply *r2)
{
	size_t i=0;
	int ret=0;
	if(r1->type!=r2->type) return r1->type-r2->type;
	switch(r1->type)
	{
		case SWARMKV_REPLY_INTEGER:
			return r1->integer - r2->integer;
			break;
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_NODE:
			if(r1->len!=r2->len)
			{
				return r1->len - r2->len;
			}
			else
			{
				return memcmp(r1->str, r2->str, r1->len);
			}
			break;
		case SWARMKV_REPLY_NIL:
			return 0;
			break;
		case SWARMKV_REPLY_ARRAY:
			if(r1->n_element != r2->n_element)
			{
				return r1->n_element - r2->n_element;
			}
			for(i=0; i<r1->n_element; i++)
			{
				ret=reply_compare(r1->elements[i], r2->elements[i]);
				if(ret!=0)
				{
					return ret;
				}
			}
			break;
		default:
			ret=0;
	}
	return 0;
}
void generic_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	if(0==reply_compare(reply, &(arg->expected_reply)))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}

class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		void* logger=NULL;
		const char* log_path="./basic-test.log";
		char *err=NULL;
		const char *cluster_name="swarmkv-basic-test";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1 5210");
		logger=MESA_create_runtime_log_handle(log_path, 0);
		struct swarmkv_options* opts=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 5211);
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
	}	
 	// Some expensive resource shared by all tests.
	static struct swarmkv* db;
};
struct swarmkv* SwarmkvBasicTest::db;
TEST_F(SwarmkvBasicTest, SET_GET)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_cstring(arg, val);
	swarmkv_get(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, DEL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name2";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, EXPIRE_TTL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="quarantine";
	const char* val="wuhan-jiangxia-rujia";
	int exec_successful=0;
	int seconds=3;
	
	//SET key value
	arg=cmd_exec_arg_new();
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	sleep(seconds+1);

	//GET key
	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);


	//EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_expire(db, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//TTL key
	cmd_exec_arg_expect_integer(arg, -2);
	swarmkv_ttl(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	

	cmd_exec_arg_free(arg);
	arg=NULL;
}

class SwarmkvTwoNodes : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		void* logger=NULL;
		char *err=NULL;
		const char *log_path="./swarmkv-2-nodes.log";
		
		const char *cluster_name="swarmkv-2-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1 5210 127.0.0.1 5220");
		
		logger=MESA_create_runtime_log_handle(log_path, 0);
		struct swarmkv_options* opts1=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts1, 5210);
		swarmkv_options_set_health_check_port(opts1, 5211);
		swarmkv_options_set_logger(opts1, logger);
		db1=swarmkv_open(opts1, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}

		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts2, 5220);
		swarmkv_options_set_health_check_port(opts2, 5221);
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
	}	
 	// Some expensive resource shared by all tests.
	static struct swarmkv *db1, *db2;
};
struct swarmkv* SwarmkvTwoNodes::db1;
struct swarmkv* SwarmkvTwoNodes::db2;
TEST_F(SwarmkvTwoNodes, SET_GET)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_cstring(arg, val1);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db2, key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	wait_for_sync();
	
	cmd_exec_arg_expect_cstring(arg, val2);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvTwoNodes, Set1K)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	char key[128]="", val[128]="";
	int exec_successful=0, i=0, round=100, n_success=0;

	arg=cmd_exec_arg_new();	
	cmd_exec_arg_expect_OK(arg);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "set1k-%d", i);
		snprintf(val, sizeof(val), "value-xx-%d", i);
		tmp_db=i%2?db1:db2;
		swarmkv_set(tmp_db, key, strlen(key), val, strlen(val), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "set1k-%d", i);
		snprintf(val, sizeof(val), "value-xx-%d", i);
		tmp_db=i%2?db2:db1;
		cmd_exec_arg_expect_cstring(arg, val);
		swarmkv_get(tmp_db, key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);
	cmd_exec_arg_free(arg);
}

TEST_F(SwarmkvTwoNodes, DEL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id002";
	const char* val="to-be-deleted";	
	int exec_successful=0;
	arg=cmd_exec_arg_new();
	

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;

}

TEST_F(SwarmkvTwoNodes, INCRBY)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id003";
	long long val=10000;	
	int exec_successful=0;
	arg=cmd_exec_arg_new();

	cmd_exec_arg_expect_integer(arg, val);
	swarmkv_incrby(db1, key, strlen(key), val, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_integer(arg, val+100);
	swarmkv_incrby(db2, key, strlen(key), 100, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_expect_integer(arg, val+100-200);
	swarmkv_incrby(db1, key, strlen(key), -200, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	cmd_exec_arg_free(arg);
	arg=NULL;

}
TEST_F(SwarmkvTwoNodes, EXPIRE)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	char key[128]="", val[128]="";
	int exec_successful=0, i=0, round=128, n_success=0;
	int max_timeout_seconds=10, min_timeout_seconds=2;
	int seconds=0;
	arg=cmd_exec_arg_new();	

	/*
	 * SET expiring-key-%d value-xx-%d
	*/
	cmd_exec_arg_expect_OK(arg);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		snprintf(val, sizeof(val), "value-xx-%d", i);
		tmp_db=i%2?db1:db2;
		swarmkv_set(tmp_db, key, strlen(key), val, strlen(val), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);


	/*
	 * EXPIRE expiring-key-%d [min_timeout_seconds, max_timeout_seconds)
	 * Expect integer 1 reply
	*/

	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		cmd_exec_arg_expect_integer(arg, 1);
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		swarmkv_expire(tmp_db, key, strlen(key), seconds, generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);

	sleep(max_timeout_seconds+1);

	/*
	 * GET expiring-key-%d
	 * Expect NIL reply
	*/
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		cmd_exec_arg_expect_NIL(arg);
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		swarmkv_get(tmp_db, key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);


	/*
	 * KEYSPACE RLIST expiring-key-%d
	 * Expect NIL reply, make sure that expired keys are removed from keyspace.
	*/

	n_success=0;
	struct swarmkv_reply *reply=NULL;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		reply=swarmkv_command(tmp_db, "KEYSPACE RLIST %s", key);
		if(reply->type==SWARMKV_REPLY_NIL)
		{
			n_success++;
		}
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(round, n_success);

	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvTwoNodes, TTL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;

	const char *key="ttl-key-001";
	const char *value="hello-world";
	int exec_successful=0;
	int seconds=3;

	arg=cmd_exec_arg_new();

	// db1: SET key value
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), value, strlen(value), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	// db2: EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db2, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//db1: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);


	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);

}
TEST_F(SwarmkvTwoNodes, PERSIST)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;

	const char *key="persit-key-001";
	const char *value="hello-world";
	int exec_successful=0;
	int seconds=3;

	arg=cmd_exec_arg_new();

	// db1: SET key value
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), value, strlen(value), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	// db1: EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db1, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);


	//db1: PERSIT key
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_persist(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);


	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, -1);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	sleep(seconds+1);

	//db1: GET key
	cmd_exec_arg_expect_cstring(arg, value);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);

}

TEST_F(SwarmkvTwoNodes, FromLocalReplica)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id004";
	int exec_successful=0;
	arg=cmd_exec_arg_new();



	//db1: SET id003 lisi	
	const char* val1="lisi";

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db2: GET id003
	cmd_exec_arg_expect_cstring(arg, val1);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 0);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	//db2: SET id003 wang2mazi	
	const char* val2="wang2mazi";

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db2, key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 1);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	//db1: GET id003	
	cmd_exec_arg_expect_cstring(arg, val2);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 1);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);

}

TEST_F(SwarmkvTwoNodes, Wait)
{
	sleep(3600*2);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
