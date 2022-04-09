#include "swarmkv/swarmkv.h"
#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#define	CMD_EXEC_TIMEOUT_MS	1000*1800
struct cmd_exec_arg
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int is_callback_executed;
	int success;
	int api_invoking_tid;
	int callback_invoking_tid;
	int is_sync_callback;
	const struct swarmkv_reply *expected_reply;
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
		return arg->success;;
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

void basic_test_get_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	char buff[1024];
	swarmkv_reply_print(reply, buff, sizeof(buff));
//	printf("get %s\n", buff);
	if(reply->type==SWARMKV_REPLY_STRING && 0==memcmp(reply->str, "zhangsan", reply->len))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}
void basic_test_set_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	if(reply->type==SWARMKV_REPLY_STATUS && 0==memcmp(reply->str, "OK", reply->len))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}
void basic_test_del_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer==1)
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}
void expect_get_null_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	if(reply->type==SWARMKV_REPLY_NIL)
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
		logger=MESA_create_runtime_log_handle(log_path, 0);
		struct swarmkv_options* opts=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 5211);
		swarmkv_options_set_logger(opts, logger);
		db=swarmkv_open(opts, "swarmkv-basic-test", &err);
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
TEST_F(SwarmkvBasicTest, SetGet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db, key, strlen(key), val, strlen(val), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	swarmkv_get_string(db, key, strlen(key), basic_test_get_callback, arg);
	cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, Delete)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name2";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db, key, strlen(key), val, strlen(val), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	
	swarmkv_del(db, key, strlen(key), basic_test_del_callback, arg);
	cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
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
		const char *log_path="./basic-test.log";
		logger=MESA_create_runtime_log_handle(log_path, 0);
		struct swarmkv_options* opts1=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts1, 5210);
		swarmkv_options_set_health_check_port(opts1, 5211);
		swarmkv_options_set_logger(opts1, logger);
		db1=swarmkv_open(opts1, "swarmkv-2-nodes", &err);
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
		db2=swarmkv_open(opts2, "swarmkv-2-nodes", &err);
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
TEST_F(SwarmkvTwoNodes, SetGet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db1, key, strlen(key), val1, strlen(val1), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	swarmkv_get_string(db2, key, strlen(key), basic_test_get_callback, arg);
	cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db1, key, strlen(key), val2, strlen(val2), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);
	arg=NULL;
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
	swarmkv_set_string(db1, key, strlen(key), val, strlen(val), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	
	swarmkv_del(db2, key, strlen(key), basic_test_del_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	swarmkv_get_string(db1, key, strlen(key), expect_get_null_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	swarmkv_get_string(db2, key, strlen(key), expect_get_null_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;

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
	if(0==reply_compare(reply, arg->expected_reply))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}

TEST_F(SwarmkvTwoNodes, INCRBY)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id003";
	const char* val="10000";	
	int exec_successful=0;
	arg=cmd_exec_arg_new();
	struct swarmkv_reply reply;
	arg->expected_reply=&reply;

	reply.type=SWARMKV_REPLY_STATUS;
	reply.str=(char*)"OK";
	reply.len=strlen("OK");
	swarmkv_set_string(db1, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	reply.type=SWARMKV_REPLY_INTEGER;
	reply.integer=10000+100;
	swarmkv_incrby(db2, key, strlen(key), 100, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	reply.type=SWARMKV_REPLY_INTEGER;
	reply.integer=10000+100-200;
	swarmkv_incrby(db1, key, strlen(key), -200, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);


	cmd_exec_arg_free(arg);
	arg=NULL;

}

TEST_F(SwarmkvTwoNodes, CacheSync)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id004";
	int exec_successful=0;
	struct swarmkv_reply expected_reply;
	arg=cmd_exec_arg_new();
	arg->expected_reply=&expected_reply;



	//db1: set id003 lisi	
	const char* val1="lisi";
	
	expected_reply.type=SWARMKV_REPLY_STATUS;
	expected_reply.str=(char*)"OK";
	expected_reply.len=strlen("OK");

	swarmkv_set_string(db1, key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//db2: get id003
	expected_reply.type=SWARMKV_REPLY_STRING;
	expected_reply.str=(char*)val1;
	expected_reply.len=strlen(val1);
	swarmkv_get_string(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	EXPECT_EQ(arg->is_sync_callback, 0);
	cmd_exec_arg_clear(arg);

	//db1: set id003 wang2mazi	
	const char* val2="wang2mazi";

	expected_reply.type=SWARMKV_REPLY_STATUS;
	expected_reply.str=(char*)"OK";
	expected_reply.len=strlen("OK");

	swarmkv_set_string(db1, key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);

	//db2: get id003
	
	expected_reply.type=SWARMKV_REPLY_STRING;
	expected_reply.str=(char*)val2;
	expected_reply.len=strlen(val2);

	swarmkv_get_string(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_EQ(exec_successful, 1);
	EXPECT_EQ(arg->is_sync_callback, 1);
	cmd_exec_arg_clear(arg);

}

TEST_F(SwarmkvTwoNodes, Wait)
{
	sleep(1800);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
