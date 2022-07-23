#include "swarmkv/swarmkv.h"
#include "test_utils.h"

#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <uuid/uuid.h>
#define PERF_TEST_EXEC_TO_MS 1000
void perf_test_generic_callback(const struct swarmkv_reply* reply, void * cb_arg)
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
	cmd_exec_arg_free(arg);
	return;
}
void set_counter(struct cmd_exec_arg *exec_arg, void *cb_arg)
{
	unsigned long long *counter=(unsigned long long *)cb_arg;
	if(exec_arg->success) (*counter)++;
	return;
}
void* blocking_call_thread(void *thread_arg)
{
	struct swarmkv* db=(struct swarmkv *)thread_arg;
	size_t n_key=1024*60, i=0;
	char key[256]={0}, value[256]={0};
	uuid_t uuid;
	char uuid_str[36];
	int *success=ALLOC(int, 1);
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	struct cmd_exec_arg *arg=NULL;
	unsigned long long set_success_counter=0, get_success_counter=0, del_success_counter=0;
	
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);	
		arg=cmd_exec_arg_new();
		//cmd_exec_arg_set_cb(arg, set_counter, &set_success_counter);
		cmd_exec_arg_expect_OK(arg);
		swarmkv_set(db, key, strlen(key), value, strlen(value), perf_test_generic_callback, arg);
		set_success_counter+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}
	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);	
		arg=cmd_exec_arg_new();
//		cmd_exec_arg_set_cb(arg, set_counter, &get_success_counter);
		cmd_exec_arg_expect_cstring(arg, value);
		swarmkv_get(db, key, strlen(key), perf_test_generic_callback, arg);	
		get_success_counter+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}

	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		arg=cmd_exec_arg_new();
		//cmd_exec_arg_set_cb(arg, set_counter, &del_success_counter);
		cmd_exec_arg_expect_integer(arg, 1);
		swarmkv_del(db, key, strlen(key), perf_test_generic_callback, arg);
		del_success_counter+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}
	
	sleep(10);
	*success=0;
	if( n_key==get_success_counter && 
		get_success_counter==set_success_counter &&
		get_success_counter==del_success_counter)
	{
		*success=1;
	}
	return success;
}

TEST(MultiThreads, Nnodes)
{
	int PERF_TEST_NODE_NUMBER=3;
	int PERF_TEST_CALLBACK_THREAD_NUMBER=4;
	int PERF_TEST_WORKER_THREAD_NUMBER=2;
	int i=0, j=0;
	struct swarmkv *db[PERF_TEST_NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-n-threads.log";

	char node_list_str[1024]={0};
	for(i=0; i<PERF_TEST_NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1 %d ", 5310+i);
	}
	const char *cluster_name="swarmkv-n-threads";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	void *logger=MESA_create_runtime_log_handle(log_path, 0);
	struct swarmkv_options* opts[PERF_TEST_NODE_NUMBER];
	for(i=0; i<PERF_TEST_NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_p2p_port(opts[i], 5310+i);
		swarmkv_options_set_health_check_port(opts[i], 6310+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], PERF_TEST_WORKER_THREAD_NUMBER);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %d instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
	}



	pthread_t threads[PERF_TEST_NODE_NUMBER][PERF_TEST_WORKER_THREAD_NUMBER];	
	for(i=0; i<PERF_TEST_NODE_NUMBER; i++)
	{
		for(j=0; j<PERF_TEST_CALLBACK_THREAD_NUMBER; j++)
		{
			pthread_create(&(threads[i][j]), NULL, blocking_call_thread, db[i]);
		}
	}
	int *successful_of_blocking_call_thread=NULL;
	for(i=0; i<PERF_TEST_NODE_NUMBER; i++)
	{
		for(j=0; j<PERF_TEST_WORKER_THREAD_NUMBER; j++)
		{
			pthread_join(threads[i][j], (void**)&successful_of_blocking_call_thread);	

			EXPECT_EQ(*successful_of_blocking_call_thread, 1);
			*successful_of_blocking_call_thread=0;
			free(successful_of_blocking_call_thread);
			successful_of_blocking_call_thread=NULL;
		}
	}
//		sleep(60*100);
	for(i=0; i<PERF_TEST_NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}
	MESA_destroy_runtime_log_handle(logger);


}


int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

