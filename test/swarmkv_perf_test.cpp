#include "swarmkv/swarmkv.h"
#include "test_utils.h"
#include "log.h"

#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <uuid/uuid.h>
#define PERF_TEST_EXEC_TO_MS 10000

void set_counter(struct cmd_exec_arg *exec_arg, void *cb_arg)
{
	unsigned long long *counter=(unsigned long long *)cb_arg;
	if(exec_arg->success) (*counter)++;
	return;
}

void *blocking_call_thread(void *thread_arg)
{
	struct swarmkv *db=(struct swarmkv *)thread_arg;
	size_t n_key=1024*60, i=0;
	char key[256]={0}, value[256]={0};
	uuid_t uuid;
	char uuid_str[36];
	int *success=ALLOC(int, 1);
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	struct cmd_exec_arg *arg=NULL;
	unsigned long long set_success_counter=0, get_success_counter=0, del_success_counter=0;
	arg=cmd_exec_arg_new();
	arg->print_reply_on_fail=1;
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);	
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_OK(arg);
		swarmkv_set(db, key, strlen(key), value, strlen(value), cmd_exec_generic_callback, arg);
		set_success_counter+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}
	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);	
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_cstring(arg, value);
		swarmkv_get(db, key, strlen(key), cmd_exec_generic_callback, arg);	
		get_success_counter+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}

	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_integer(arg, 1);
		swarmkv_del(db, key, strlen(key), cmd_exec_generic_callback, arg);
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

TEST(MultiThreads, Nthreads)
{
	int NODE_NUMBER=3;
	int CALLBACK_THREAD_NUMBER=4;
	int WORKER_THREAD_NUMBER=2;
	int i=0, j=0;
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-n-threads.log";

	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%d ", 5310+i);
	}
	const char *cluster_name="swarmkv-n-threads";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options* opts[NODE_NUMBER];
	for(i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_p2p_port(opts[i], 5310+i);
		swarmkv_options_set_health_check_port(opts[i], 6310+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %d instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
	}



	pthread_t threads[NODE_NUMBER][WORKER_THREAD_NUMBER];	
	for(i=0; i<NODE_NUMBER; i++)
	{
		for(j=0; j<CALLBACK_THREAD_NUMBER; j++)
		{
			pthread_create(&(threads[i][j]), NULL, blocking_call_thread, db[i]);
		}
	}
	int *successful_of_blocking_call_thread=NULL;
	for(i=0; i<NODE_NUMBER; i++)
	{
		for(j=0; j<WORKER_THREAD_NUMBER; j++)
		{
			pthread_join(threads[i][j], (void**)&successful_of_blocking_call_thread);	

			EXPECT_EQ(*successful_of_blocking_call_thread, 1);
			*successful_of_blocking_call_thread=0;
			free(successful_of_blocking_call_thread);
			successful_of_blocking_call_thread=NULL;
		}
	}
//		sleep(60*100);
	for(i=0; i<NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}
	log_handle_destroy(logger);


}
int g_running_flag=0;
void *background_running_thread(void *thread_arg)
{
	struct swarmkv *db=(struct swarmkv *)thread_arg;
	uuid_t uuid;
	char uuid_str[36];
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);
	struct cmd_exec_arg *arg=NULL;
	arg=cmd_exec_arg_new();

	char key[256], value[256];
	int ret=0;
	int static_key_cnt=1024;
	for(int i=0; i<static_key_cnt; i++)
	{
		snprintf(key, sizeof(key), "static-key-%d-of-%s", i, uuid_str);
		snprintf(value, sizeof(value), "background-running-value"); 

		cmd_exec_arg_clear(arg);		
		cmd_exec_arg_expect_OK(arg);
		swarmkv_set(db, key, strlen(key), value, strlen(value), cmd_exec_generic_callback, arg);
		ret+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
	}
	EXPECT_EQ(ret, static_key_cnt);

	snprintf(value, sizeof(value), "background-running-value");	
	ret=3;
	int seq=0;
	while(g_running_flag)
	{
		ret=0;
		snprintf(key, sizeof(key), "ephemeral-key-of-%s-%d", uuid_str, seq);
		seq++;
		cmd_exec_arg_clear(arg);		
		cmd_exec_arg_expect_OK(arg);
		swarmkv_set(db, key, strlen(key), value, strlen(value), cmd_exec_generic_callback, arg);
		ret+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
		if(ret!=1)
		{
			printf("set %s failed\n", key);
		}		
		cmd_exec_arg_clear(arg);		
		cmd_exec_arg_expect_cstring(arg, value);		
		swarmkv_get(db, key, strlen(key), cmd_exec_generic_callback, arg); 
		ret+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
		if(ret!=2)
		{
			printf("get %s failed\n", key);
		}

		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_integer(arg, 1);
		swarmkv_del(db, key, strlen(key), cmd_exec_generic_callback, arg);
		ret+=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
		if(ret!=3)
		{
			printf("del %s failed\n", key);
		}
	}
	cmd_exec_arg_free(arg);
	int *success=ALLOC(int, 1);
	EXPECT_EQ(ret, 3);
	if(ret==3)
		*success=1;
	else
		*success=0;
	return success;
}
TEST(Migration, AddKeyOwner)
{
	int NODE_NUMBER=2;
	int CALLER_THREAD_NUMBER=1;
	int WORKER_THREAD_NUMBER=1;
	int i=0, j=0;
	struct swarmkv *db[NODE_NUMBER+1];
	char *err=NULL;
	const char *log_path="./swarmkv-migration.log";
	unsigned int p2p_port_start=7310, health_port_start=8310;
	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%d ", p2p_port_start+i);
	}
	const char *cluster_name="swarmkv-migration";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options *opts[NODE_NUMBER+1];
	for(i=0; i<NODE_NUMBER+1; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_p2p_port(opts[i], p2p_port_start+i);
		swarmkv_options_set_health_check_port(opts[i], health_port_start+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_p2p_timeout_us(opts[i], 100*1000);
		swarmkv_options_set_disable_run_for_leader(opts[i]);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %d instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
	}
	g_running_flag=1;
	pthread_t threads[NODE_NUMBER][WORKER_THREAD_NUMBER];
	for(i=0; i<NODE_NUMBER; i++)
	{
		for(j=0; j<CALLER_THREAD_NUMBER; j++)
		{
			pthread_create(&(threads[i][j]), NULL, background_running_thread, db[i]);
		}
	}
	sleep(10);
	swarmkv_cli_add_node(cluster_name, "127.0.0.1", p2p_port_start+NODE_NUMBER);
	sleep(300);
	g_running_flag=0;
	int *successful_backgroud_running_thread=NULL;
	for(i=0; i<NODE_NUMBER; i++)
	{
		for(j=0; j<WORKER_THREAD_NUMBER; j++)
		{
			pthread_join(threads[i][j], (void**)&successful_backgroud_running_thread);	

			EXPECT_EQ(*successful_backgroud_running_thread, 1);
			*successful_backgroud_running_thread=0;
			free(successful_backgroud_running_thread);
			successful_backgroud_running_thread=NULL;
		}
	}
	sleep(10);
	for(i=0; i<NODE_NUMBER+1; i++)
	{
		swarmkv_close(db[i]);
	}

	log_handle_destroy(logger);

}
int g_tconsume_running_flag=0;
size_t g_token_bucket_number=200*1000;

void *background_tconsume_thread(void *thread_arg)
{
	struct swarmkv *db=(struct swarmkv *)thread_arg;

	struct cmd_exec_arg *arg=NULL;
	arg=cmd_exec_arg_new();

	char key[256];
	int ret=0;
	long long tokens=1;
	size_t round=0, got_token_cnt=0;
	int start=random()%16;
	while(g_tconsume_running_flag)
	{
		snprintf(key, sizeof(key), "tb-%zu", (round+start)%g_token_bucket_number);
		cmd_exec_arg_expect_integer(arg, tokens);
		swarmkv_tconsume(db, key, strlen(key), tokens, cmd_exec_generic_callback, arg);
		ret=cmd_exec_arg_wait(arg, PERF_TEST_EXEC_TO_MS);
		cmd_exec_arg_clear(arg);		
		if(ret==1) got_token_cnt++;
		round++;
	}
	cmd_exec_arg_free(arg);
	int *success=ALLOC(int, 1);
	EXPECT_EQ(got_token_cnt, round);
	if(got_token_cnt==round)
		*success=1;
	else
		*success=0;
	return success;
}
TEST(Performance, Sync)
{
	size_t NODE_NUMBER=2;
	size_t CALL_THREAD_PER_NODE=2;
	size_t WORKER_THREAD_NUMBER=1;
	
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-sync.log";
	unsigned int p2p_port_start=7310, health_port_start=8310;
	char node_list_str[1024]={0};
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", p2p_port_start+i);
	}
	const char *cluster_name="swarmkv-sync";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options *opts[NODE_NUMBER];
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts[i], p2p_port_start+i);
		swarmkv_options_set_health_check_port(opts[i], health_port_start+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_p2p_timeout_us(opts[i], 1000*1000);
		swarmkv_options_set_sync_interval_us(opts[i], 200*1000);
		swarmkv_options_set_disable_run_for_leader(opts[i]);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %zu instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
	}
	char key[256]={0};
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<g_token_bucket_number; i++)
	{
		snprintf(key, sizeof(key), "tb-%zu", i);
		reply=swarmkv_command(db[i%NODE_NUMBER], NULL, "TCFG %s 2000000 1000000", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		if(reply->type != SWARMKV_REPLY_STATUS)
		{
			swarmkv_reply_print(reply, stdout);
		}
		swarmkv_reply_free(reply);
	}
	srand(171);
	g_tconsume_running_flag=1;
	pthread_t threads[NODE_NUMBER][CALL_THREAD_PER_NODE];
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		for(size_t j=0; j<CALL_THREAD_PER_NODE; j++)
		{
			pthread_create(&(threads[i][j]), NULL, background_tconsume_thread, db[i]);
		}
	}

	sleep(600);
	g_tconsume_running_flag=0;
	int *successful_backgroud_running_thread=NULL;
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		for(size_t j=0; j<CALL_THREAD_PER_NODE; j++)
		{
			pthread_join(threads[i][j], (void**)&successful_backgroud_running_thread);	

			EXPECT_EQ(*successful_backgroud_running_thread, 1);
			*successful_backgroud_running_thread=0;
			free(successful_backgroud_running_thread);
			successful_backgroud_running_thread=NULL;
		}
	}
	sleep(10);
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}

	log_handle_destroy(logger);

}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

