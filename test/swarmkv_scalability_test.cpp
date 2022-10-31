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
#include <time.h>

#define SCALABLE_MAX_KEY 1000000

long long g_cmd_success;
long long g_cmd_failed;
long long g_cmd_wait_us;
struct cmd_exec_arg *g_signal;

struct scalable_result_ctx
{
	int success;
	int failed;
	struct swarmkv_reply expected_reply;
	struct timespec start_time;
	struct timeval request_start_time;
};

struct scalable_result_ctx* scalable_result_ctx_new(void)
{
	struct scalable_result_ctx* cb_arg=(struct scalable_result_ctx*)calloc(sizeof(struct scalable_result_ctx), 1);
	cb_arg->success=0;
	cb_arg->failed=0;
	return cb_arg;
}

void swarmkv_scalable_expect_OK(struct scalable_result_ctx* arg)
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

void swarmkv_scalable_expect_cstring(struct scalable_result_ctx* arg, const char* string)
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

void scalable_gather_result_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct timeval end_time;
	long long jiffies_us=0;

	struct scalable_result_ctx *scalable_ctx=(struct scalable_result_ctx *)cb_arg;
	if(0==reply_compare(reply, &(scalable_ctx->expected_reply)))
	{
		g_cmd_success++;
	}
	else
	{
		g_cmd_failed++;
	}

	gettimeofday(&(end_time), NULL);
	jiffies_us = (end_time.tv_sec - scalable_ctx->request_start_time.tv_sec) * 1000 * 1000 + (end_time.tv_usec - scalable_ctx->request_start_time.tv_usec);
	g_cmd_wait_us +=jiffies_us;
	free(scalable_ctx);
	scalable_ctx=NULL;
	if(g_cmd_failed+g_cmd_success==SCALABLE_MAX_KEY)
	{
		cmd_exec_arg_success(g_signal);
	}
	return;
}

void *scalable_worker_thread(void *thread_arg)
{
	char value[256]={0};
	struct scalable_result_ctx *scalable_ctx=NULL;

	printf("Scalable Work thread Start\n");

	uuid_t uuid;
	char uuid_str[36];
	struct swarmkv *db=(struct swarmkv *)thread_arg;
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	g_cmd_wait_us=0;
	g_cmd_success=0;

	long long cmd_rate = 20000;
	long long cmd_cnt=0, cmd_cnt_of_this_second=0;
	long long latency_ms=0, async_finished=0;
    struct timeval now, last, global_start, global_end;
	long long last_cmd_wait_us=0, last_cmd_success=0, last_cmd_failed=0;

	g_signal=cmd_exec_arg_new();
	void cmd_exec_arg_set_cb(struct cmd_exec_arg *arg, proc_result_callback_t *cb, void* cb_arg);

	gettimeofday(&global_start, NULL);
//	while(cmd_cnt<SCALABLE_MAX_KEY)
	while(1)
	{
		snprintf(value, sizeof(value), "scalable-value-%lld", cmd_cnt%SCALABLE_MAX_KEY);
		scalable_ctx=scalable_result_ctx_new();
		gettimeofday(&scalable_ctx->request_start_time, NULL);
		swarmkv_scalable_expect_cstring(scalable_ctx, value);
		swarmkv_command_async(db, scalable_gather_result_callback, scalable_ctx, NULL,
							"GET scalable-key-%lld", cmd_cnt%SCALABLE_MAX_KEY);
		cmd_cnt++;
		cmd_cnt_of_this_second++;
		if(cmd_cnt_of_this_second==cmd_rate)
		{
			gettimeofday(&now, NULL);
			async_finished=g_cmd_success-last_cmd_success+g_cmd_failed-last_cmd_failed;
			latency_ms=(g_cmd_wait_us-last_cmd_wait_us)/async_finished/1000;
			printf("Async execute %lld commands, %lld success, %lld failed, %lld cmd/s, per-command latency %lld ms\n",
				cmd_cnt_of_this_second, g_cmd_success, g_cmd_failed, 
				async_finished/((now.tv_sec - last.tv_sec)==0?1:(now.tv_sec - last.tv_sec)),
				latency_ms);
			gettimeofday(&last, NULL);
			last_cmd_failed=g_cmd_failed;
			last_cmd_success=g_cmd_success;
			last_cmd_wait_us=g_cmd_wait_us;
			usleep(1000*1000-now.tv_usec);
			if(latency_ms<450)
			{
				cmd_rate+=1000;
			}
			else if(latency_ms>500)
			{
				cmd_rate-=4000;
			}
			if(cmd_rate<=0) cmd_rate=1000;
			cmd_cnt_of_this_second=0;
		}
	}
	cmd_exec_arg_wait(g_signal, 1000*1000);
	cmd_exec_arg_free(g_signal);
	gettimeofday(&global_end, NULL);


	int *success=ALLOC(int, 1);
	*success=1;
	return success;
}

TEST(Performance, Scalability)
{
	char *err=NULL;
	int worker_thread_number=1;
	const char *log_path="./scalability-test.log";
	struct log_handle * logger=log_handle_create(log_path, 0);

	const char *cluster_name="demo";
	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_dryrun(opts);
	swarmkv_options_set_p2p_port(opts, 5212);
	swarmkv_options_set_health_check_port(opts, 6212);
	swarmkv_options_set_logger(opts, logger);
	swarmkv_options_set_worker_thread_number(opts, 2);
	swarmkv_options_set_p2p_timeout_us(opts, 500*1000);
	struct swarmkv *db=swarmkv_open(opts, cluster_name, &err);
	if(err)
	{
		printf("swarmkv_open instance failed: %s\n", err);
		free(err);
		err=NULL;
	}

	int i=0;
	pthread_t threads[worker_thread_number];
	for(i=0; i<worker_thread_number; i++)
	{
		pthread_create(&(threads[i]), NULL, scalable_worker_thread, db);
	}
	sleep(3600);

	int *successful_scalable_worker_thread=NULL;

	for(i=0; i<worker_thread_number; i++)
	{
		pthread_join(threads[i], (void**)&successful_scalable_worker_thread);
		EXPECT_EQ(*successful_scalable_worker_thread, 1);
		*successful_scalable_worker_thread=0;
		free(successful_scalable_worker_thread);
		successful_scalable_worker_thread=NULL;
	}
	swarmkv_close(db);
	log_handle_destroy(logger);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

