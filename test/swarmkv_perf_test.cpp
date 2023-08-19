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
	swarmkv_register_thread(db);
	int *success=ALLOC(int, 1);
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	struct swarmkv_reply *reply=NULL;
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);
		reply=swarmkv_command(db, "SET %s %s", key, value);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		EXPECT_STREQ(reply->str, "OK");
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		snprintf(value, sizeof(value), "%s-val-%zu", uuid_str, i);
		reply=swarmkv_command(db, "GET %s", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, value);
		swarmkv_reply_free(reply);
	}

	wait_for_sync();
	for(i=0; i<n_key; i++)
	{
		snprintf(key, sizeof(key), "%s-key-%zu", uuid_str, i);
		reply=swarmkv_command(db, "DEL %s", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 1);
		swarmkv_reply_free(reply);
	}

	sleep(10);
	*success=1;

	return success;
}
#define	PERF_NTHREAD_CLUSTER_PORT_START 5310
#define	PERF_NTHREAD_HEALTH_PORT_START 6310
#define PERF_DATA_SYNC_CLUSTER_PORT_START 7310
#define PERF_DATA_SYNC_HEALTH_PORT_START 8310
#define PERF_ASYNC_EXEC_CLUSTER_PORT_START 9310
#define PERF_ASYNC_EXEC_HEALTH_PORT_START 10310
#define PERF_RESILIENCE_ADD_SLOT_OWNER_CLUSTER_PORT_START 11310
#define	PERF_RESILIENCE_ADD_SLOT_OWNER_HEALTH_PORT_START 12310
#define PERF_RESILIENCE_FAILOVER_CLUSTER_PORT_START 13310
#define	PERF_RESILIENCE_FAILOVER_HEALTH_PORT_START 14310

TEST(Performance, Nthreads)
{
	int NODE_NUMBER=2;
	int CALLER_THREAD_NUMBER=2;
	int WORKER_THREAD_NUMBER=2;
	int i=0, j=0;
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-n-threads.log";

	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%d ", PERF_NTHREAD_CLUSTER_PORT_START+i);
	}
	const char *cluster_name="swarmkv-n-threads";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options* opts[NODE_NUMBER];
	for(i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_cluster_port(opts[i], PERF_NTHREAD_CLUSTER_PORT_START+i);
		swarmkv_options_set_health_check_port(opts[i], PERF_NTHREAD_HEALTH_PORT_START+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_caller_thread_number(opts[i], CALLER_THREAD_NUMBER);
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
		for(j=0; j<CALLER_THREAD_NUMBER; j++)
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
//	sleep(30*60);
	for(i=0; i<NODE_NUMBER; i++)
	{
		//close slowly to cover more code branches.
		sleep(2);
		swarmkv_close(db[i]);
	}
	log_handle_destroy(logger);
}

int g_tconsume_running_flag=0;
size_t g_token_bucket_number=200*1000;

void *background_tconsume_thread(void *thread_arg)
{
	struct swarmkv *db=(struct swarmkv *)thread_arg;
	swarmkv_register_thread(db);

	char key[256];
	size_t round=0, got_token_cnt=0;
	int start=random()%16;
	struct swarmkv_reply *reply=NULL;
	while(g_tconsume_running_flag)
	{
		snprintf(key, sizeof(key), "tb-%zu", (round+start)%g_token_bucket_number);
		reply=swarmkv_command(db, "TCONSUME %s 1", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		got_token_cnt+=reply->integer;
		swarmkv_reply_free(reply);
		round++;
	}
	int *success=ALLOC(int, 1);
	EXPECT_EQ(got_token_cnt, round);
	if(got_token_cnt==round)
		*success=1;
	else
		*success=0;
	return success;
}
TEST(Performance, DataSync)
{
	size_t NODE_NUMBER=2;
	size_t CALLER_THREAD_NUMBER=2;
	size_t WORKER_THREAD_NUMBER=1;
	
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-sync.log";
	char node_list_str[1024]={0};
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", PERF_DATA_SYNC_CLUSTER_PORT_START+i);
	}
	const char *cluster_name="swarmkv-sync";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options *opts[NODE_NUMBER];
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts[i], PERF_DATA_SYNC_CLUSTER_PORT_START+i);
		swarmkv_options_set_health_check_port(opts[i], PERF_DATA_SYNC_HEALTH_PORT_START+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_caller_thread_number(opts[i], CALLER_THREAD_NUMBER+1);
		swarmkv_options_set_cluster_timeout_us(opts[i], 1000*1000);
		swarmkv_options_set_sync_interval_us(opts[i], 100*1000);
		swarmkv_options_set_run_for_leader_enabled(opts[i], 0);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %zu instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db[i]);
	}
	char key[256]={0};
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<g_token_bucket_number; i++)
	{
		snprintf(key, sizeof(key), "tb-%zu", i);
		reply=swarmkv_command_on(db[i%NODE_NUMBER], NULL, "TCFG %s 2000000 2000000", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		if(reply->type != SWARMKV_REPLY_STATUS)
		{
			swarmkv_reply_print(reply, stdout);
		}
		swarmkv_reply_free(reply);
	}
	srand(171);
	g_tconsume_running_flag=1;
	pthread_t threads[NODE_NUMBER][CALLER_THREAD_NUMBER];
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		for(size_t j=0; j<CALLER_THREAD_NUMBER; j++)
		{
			pthread_create(&(threads[i][j]), NULL, background_tconsume_thread, db[i]);
		}
	}

	sleep(20);
	g_tconsume_running_flag=0;
	int *successful_backgroud_running_thread=NULL;
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		for(size_t j=0; j<CALLER_THREAD_NUMBER; j++)
		{
			pthread_join(threads[i][j], (void**)&successful_backgroud_running_thread);	

			EXPECT_EQ(*successful_backgroud_running_thread, 1);
			*successful_backgroud_running_thread=0;
			free(successful_backgroud_running_thread);
			successful_backgroud_running_thread=NULL;
		}
	}
	sleep(20);
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}

	log_handle_destroy(logger);
}
struct async_exec_ctx
{
	int expected_reply_cnt;
	int reply_cnt;
	struct swarmkv *db;
};
void async_on_reply_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct async_exec_ctx *ctx=(struct async_exec_ctx *)arg;
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	if(reply->type != SWARMKV_REPLY_STATUS)
	{
		swarmkv_reply_print(reply, stdout);
	}
	ctx->reply_cnt++;
	if(ctx->reply_cnt==ctx->expected_reply_cnt)
	{
		swarmkv_caller_loop_break(ctx->db);
	}
}
TEST(Performance, AsyncExec)
{
	int NODE_NUMBER=1;
	int CALLER_THREAD_NUMBER=1;
	int WORKER_THREAD_NUMBER=1;
	int i=0;
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-async-exec.log";

	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%d ", PERF_ASYNC_EXEC_CLUSTER_PORT_START+i);
	}
	const char *cluster_name="swarmkv-async-exec";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options* opts[NODE_NUMBER];
	for(i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_cluster_port(opts[i], PERF_ASYNC_EXEC_CLUSTER_PORT_START+i);
		swarmkv_options_set_health_check_port(opts[i], PERF_ASYNC_EXEC_HEALTH_PORT_START+i);
		swarmkv_options_set_cluster_timeout_us(opts[i], 2000*1000);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_caller_thread_number(opts[i], CALLER_THREAD_NUMBER);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %d instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db[i]);
	}
	int key_number=65536;
	struct async_exec_ctx ctx;
	ctx.expected_reply_cnt=key_number;
	ctx.reply_cnt=0;
	ctx.db=db[0];
	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);
	for(i=0; i<key_number; i++)
	{
		swarmkv_async_command(db[0], async_on_reply_cb, &ctx, "SET async-key-%d by-node-%d", i, 0);
		//swarmkv_caller_loop(db[0], SWARMKV_LOOP_NONBLOCK|SWARMKV_LOOP_ONCE, NULL);
	}
	clock_gettime(CLOCK_REALTIME, &end);
	double elapsed_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
	printf("Async SET %d keys, elapsed %lf ms, %lf keys/s\n", key_number, elapsed_ms, key_number*1000.0/elapsed_ms);
	swarmkv_caller_loop(db[0], SWARMKV_LOOP_NO_EXIT_ON_EMPTY, NULL);

	
	for(i=0; i<NODE_NUMBER; i++)
	{
		//close slowly to cover more code branches.
		sleep(2);
		swarmkv_close(db[i]);
	}
	log_handle_destroy(logger);
}
int g_running_flag=0;
void *migration_background_thread(void *thread_arg)
{
	struct swarmkv *db=(struct swarmkv *)thread_arg;
	swarmkv_register_thread(db);
	uuid_t uuid;
	char uuid_str[36];
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	char key[256], value[256];
	int static_key_cnt=1024;
	struct swarmkv_reply *reply=NULL;
	snprintf(value, sizeof(value), "background-running-value"); 
	for(int i=0; i<static_key_cnt; i++)
	{
		snprintf(key, sizeof(key), "static-key-%d-of-%s", i, uuid_str);
		
		reply=swarmkv_command(db, "SET %s %s", key, value);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		swarmkv_reply_free(reply);
	}
	int seq=0;
	while(g_running_flag)
	{
		snprintf(key, sizeof(key), "ephemeral-key-of-%s-%d", uuid_str, seq);
		seq++;
		reply=swarmkv_command(db, "SET %s %s", key, value);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		swarmkv_reply_free(reply);
		
		reply=swarmkv_command(db, "GET %s", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, value);
		swarmkv_reply_free(reply);

		reply=swarmkv_command(db, "DEL %s", key);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 1);
		swarmkv_reply_free(reply);
	}
	int *success=ALLOC(int, 1);
	*success=1;
	return success;
}
TEST(Resilience, AddSlotOwner)
{
	int NODE_NUMBER=2;
	int CANDINATE_NUMBER=2;
	int CALLER_THREAD_NUMBER=1;
	int WORKER_THREAD_NUMBER=1;
	int i=0, j=0;
	struct swarmkv *db[NODE_NUMBER+CANDINATE_NUMBER];
	char *err=NULL;
	const char *log_path="./swarmkv-migration-11.log";
	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%d ",
		 PERF_RESILIENCE_ADD_SLOT_OWNER_CLUSTER_PORT_START+i);
	}
	const char *cluster_name="swarmkv-migration";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct log_handle * logger=log_handle_create(log_path, 0);
	struct swarmkv_options *opts[NODE_NUMBER+CANDINATE_NUMBER];
	for(i=0; i<NODE_NUMBER+CANDINATE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();	
		swarmkv_options_set_cluster_port(opts[i], PERF_RESILIENCE_ADD_SLOT_OWNER_CLUSTER_PORT_START+i);
		swarmkv_options_set_health_check_port(opts[i], PERF_RESILIENCE_ADD_SLOT_OWNER_HEALTH_PORT_START+i);
		swarmkv_options_set_logger(opts[i], logger);
		swarmkv_options_set_worker_thread_number(opts[i], WORKER_THREAD_NUMBER);
		swarmkv_options_set_caller_thread_number(opts[i], CALLER_THREAD_NUMBER);
		swarmkv_options_set_cluster_timeout_us(opts[i], 100*1000);
		swarmkv_options_set_run_for_leader_enabled(opts[i], 0);
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
			pthread_create(&(threads[i][j]), NULL, migration_background_thread, db[i]);
		}
	}
	sleep(5);
	char candinate_string[1024];
	int offset=0;
	for(i=0; i<CANDINATE_NUMBER; i++)
	{
		offset+=snprintf(candinate_string+offset, sizeof(candinate_string)-offset, "127.0.0.1:%u ", PERF_RESILIENCE_ADD_SLOT_OWNER_CLUSTER_PORT_START+NODE_NUMBER+i);
	}
	swarmkv_cli_add_slot_owner(cluster_name, candinate_string);
	sleep(10);
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
	for(i=0; i<NODE_NUMBER+CANDINATE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}

	log_handle_destroy(logger);

}
TEST(Resilience, Failover)
{
	size_t NODE_NUMBER=4;	
	struct swarmkv *db[NODE_NUMBER];
	char *err=NULL;

	char node_list_str[1024]={0};
	for(size_t i=1; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", PERF_RESILIENCE_FAILOVER_CLUSTER_PORT_START+i);
	}
	const char *cluster_name="swarmkv-failover";
	swarmkv_cli_create_cluster(cluster_name, node_list_str);
	struct swarmkv_options *opts[NODE_NUMBER];
	for(size_t i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts[i], PERF_RESILIENCE_FAILOVER_CLUSTER_PORT_START+i);
		swarmkv_options_set_health_check_port(opts[i], PERF_RESILIENCE_FAILOVER_HEALTH_PORT_START+i);
		swarmkv_options_set_worker_thread_number(opts[i], 1);
		swarmkv_options_set_caller_thread_number(opts[i], 1);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %zu instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
		ASSERT_TRUE(err==NULL);
		swarmkv_register_thread(db[i]);
	}
	size_t KEY_NUMBER=128;
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<KEY_NUMBER; i++)
	{
		for(size_t j=0; j<NODE_NUMBER; j++)
		{
			reply=swarmkv_command(db[j], "SET fo-%zu node-%zu", i, j);
			swarmkv_reply_free(reply);
		}
	}
	swarmkv_close(db[0]);
	db[0]=NULL;
	for(size_t i=0; i<KEY_NUMBER; i++)
	{
		for(size_t j=1; j<NODE_NUMBER; j++)
		{
			reply=swarmkv_command(db[j], "SET fo-%zu node-%zu", i, j);
			swarmkv_reply_free(reply);
		}
	}
	char target[32];
	sleep(10);
	for(size_t i=0; i<KEY_NUMBER; i++)
	{
		reply=swarmkv_command(db[1], "KEYSPACE RLIST fo-%zu", i);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
		EXPECT_EQ(reply->n_element, NODE_NUMBER);
		swarmkv_reply_free(reply);
		for(size_t j=1; j<NODE_NUMBER; j++)
		{
			snprintf(target, sizeof(target), "127.0.0.1:%zu", PERF_RESILIENCE_FAILOVER_CLUSTER_PORT_START+j);
			reply=swarmkv_command_on(db[1], target, "CRDT RLIST fo-%zu", i);
			EXPECT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
			EXPECT_EQ(reply->n_element, NODE_NUMBER-2);
			swarmkv_reply_free(reply);
		}
	}
	for(size_t i=1; i<NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}
}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

