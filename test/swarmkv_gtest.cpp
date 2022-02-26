#include "swarmkv/swarmkv.h"
#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>


struct cmd_exec_arg
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int is_callback_executed;
	int success;
};
struct cmd_exec_arg* cmd_exec_arg_new()
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)calloc(sizeof(struct cmd_exec_arg), 1);
	pthread_cond_init(&arg->cond, NULL);
	pthread_mutex_init(&arg->mutex, NULL);
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
	if(timed_wait_rv)
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
	pthread_cond_signal(&arg->cond);
}
void cmd_exec_arg_failed(struct cmd_exec_arg* arg)
{
	arg->is_callback_executed=1;
	arg->success=0;
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
	printf("get %s\n", buff);
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
	char buff[1024];
	swarmkv_reply_print(reply, buff, sizeof(buff));
	printf("set %s\n", buff);
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

class SwarmkvTwoNodes : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		void* logger=NULL;
		const char* log_path="./basic-test.log";
		logger=MESA_create_runtime_log_handle(log_path, 0);
		db1=swarmkv_open("swarmkv-2-nodes", "192.168.232.171", 5210, 5211, logger);
		db2=swarmkv_open("swarmkv-2-nodes", "192.168.232.171", 5220, 5221, logger);
		if(!db1)
		{
			printf("swarmkv_open failed, see %s for details.\n", log_path);
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
TEST_F(SwarmkvTwoNodes, SimpleSetGet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="name";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db1, key, strlen(key), val, strlen(val), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, 100);//100ms
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	swarmkv_get_string(db2, key, strlen(key), basic_test_get_callback, arg);
	cmd_exec_arg_wait(arg, 100);//100ms
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvTwoNodes, Wait)
{
	sleep(1800);
}

class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		void* logger=NULL;
		const char* log_path="./basic-test.log";
		logger=MESA_create_runtime_log_handle(log_path, 0);
		db=swarmkv_open("swarmkv-basic-test", "192.168.232.171", 5210, 5211, logger);
		if(!db)
		{
			printf("swarmkv_open failed, see %s for details.\n", log_path);
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
TEST_F(SwarmkvBasicTest, SimpleSetGet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	swarmkv_set_string(db, key, strlen(key), val, strlen(val), basic_test_set_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, 100);//100ms
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_clear(arg);
	swarmkv_get_string(db, key, strlen(key), basic_test_get_callback, arg);
	cmd_exec_arg_wait(arg, 100);//100ms
	EXPECT_EQ(exec_successful, 1);
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, Wait)
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
