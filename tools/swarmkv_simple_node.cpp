#include "swarmkv/swarmkv.h"
#include "swarmkv_utils.h"
#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <time.h>
#include "log.h"

#define FOREVER for(;;)

#define get_delta_ms(start, end) ((end.tv_sec-start.tv_sec)*1000 + (end.tv_usec-start.tv_usec)/1000)

static void help()
{
	fprintf(stderr, "Welcome to Swarmkv Node\n");
	fprintf(stderr,
		"simple_node <-n| -h | -p |-t> arg\n"
		"Usage:\n"
		"  -n <cluster name>\n"
		"  -h <ip:port>\n"
		"  -c <consul ip:consul port>\n"
		"  -t <worker thread number>\n"
		"  -r <caller thread number>\n"
		"  -l <consul heal check port>\n"
		"  -k <key number>\n"
		"  --node-number <node number>\n"
		"  --node-id <node id>\n"
		"  --pending-commands <number>\n"
		"  --dryrun <enable>\n"
		"  --forever <enable>\n"
		"  --cluster-announce-create <Cluster Announce ip:Cluster Announce Port>\n"
		"  --cluster-health-announce-create <Health Check Announce Ip:Health Check Announce Port>\n"
		"  --exec [Command]  Execute comand and exit.\n"
		"e.g.:./simple_node -n demo -h 127.0.0.1:5210 -t 2 -r 2\n"
		"e.g.:./simple_node -n demo -h 127.0.0.1:5210 -t 1 -r 1 -k 10000 --dryrun 1 --exec batch set command\n"
		"e.g.:./simple_node -n demo -h 127.0.0.1:5210 -t 2 -r 1 --exec batch xxcfg command\n");
	    exit(1);
}

struct swarmkv_node_config
{	
	char cluster_name[256];
	char cluster_host[64]={0};
	unsigned int cluster_port;
	char consul_host[64];
	unsigned int consul_port;
	unsigned int consul_heal_check_port;
	size_t pending_commands;
	int cluster_node_id;
	int cluster_node_number;
	long long key_number;
	int caller_thread_number;
	int forever;
	struct swarmkv *db;
};

struct swarmkv_node_config g_node_config;

typedef int batch_command_func(struct swarmkv *db, int cluster_node_id);

struct batch_cmd_spec
{
	const char *name;
	batch_command_func *func;
};

struct async_exec_globals
{
	int expected_reply_cnt;
	int reply_cnt;
	int timedout_reply_cnt;
	struct swarmkv *db;
	int cmd_inprogress;
	int on_fly_reply_cnt;
	double total_exec_time_ms;
};
struct async_exec_ctx
{
	struct timespec start;
	int seq;
	struct async_exec_globals *globals;
	struct swarmkv *db;
};

#define BATCH_SET_CMD    1
#define BATCH_XXXCFG_CMD 2

int batch_set_command(struct swarmkv *db, int cluster_node_id);

double cmds_sec=0;

void simple_node_async_on_reply_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct async_exec_ctx *ctx=(struct async_exec_ctx *)arg;
	struct async_exec_globals *globals=ctx->globals;

	struct timespec end;
	clock_gettime(CLOCK_REALTIME, &end);
	double elapsed_ms=(end.tv_sec-ctx->start.tv_sec)*1000.0+(end.tv_nsec-ctx->start.tv_nsec)/1000000.0;
	globals->total_exec_time_ms+=elapsed_ms;
	if(globals->cmd_inprogress) globals->on_fly_reply_cnt++;
	globals->reply_cnt++;
	if(globals->reply_cnt==globals->expected_reply_cnt)
	{
		swarmkv_caller_loop_break(ctx->db);
	}
	if(reply->type == SWARMKV_REPLY_ERROR)
	{
		globals->timedout_reply_cnt++;
	}
	free(ctx);
}

void wake_up_caller_thread(struct swarmkv *db)
{
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command_on(db, NULL, "set start %d", 0);
	swarmkv_reply_free(reply);
	FOREVER
	{
		reply=swarmkv_command_on(db, NULL, "get start");
		if(reply != NULL && reply->type == SWARMKV_REPLY_STRING && atoi(reply->str)==1)
		{
			swarmkv_reply_free(reply);
			break;
		}
		swarmkv_reply_free(reply);
		//usleep(10);
	}
	return;
}

void *start_batch_xxcfg_caller_thread(void *thread_arg)
{
	struct swarmkv *db=g_node_config.db;
	int i=0;
	int key_number=100*1000, round=key_number;
	int tmp_cnt=0, empty_loop=0, loops=0;
	long long last_loop_ts_ms=0, curent_ts_ms=0;
	struct timespec start, end;
	struct async_exec_ctx *ctx=NULL;
	struct swarmkv_reply *reply=NULL;

	swarmkv_register_thread(db);
	
	const char ftb_token[256]="ftb-token";
	const char tb_token[256]="tb-token";
	const char btb_token[256]="btb-token";
	char member[256]="127.0.0.1";
	long long weight=1;
	long long token=1000;

	/*TCFG*/
	reply=swarmkv_command_on(db, NULL, "TCFG tb-token 50000 50000");
	swarmkv_reply_free(reply);
	/*FTCFG*/
	reply=swarmkv_command_on(db, NULL, "FTCFG ftb-token50000 50000 256");
	swarmkv_reply_free(reply);
	/*BTCFG*/
	reply=swarmkv_command_on(db, NULL, "BTCFG btb-token 50000 50000 256");
	swarmkv_reply_free(reply);

	wake_up_caller_thread(db);

	struct async_exec_globals globals;
	memset(&globals, 0, sizeof(globals));
	globals.expected_reply_cnt=round;
	globals.reply_cnt=0;
	globals.db=db;
	globals.cmd_inprogress=1;
	clock_gettime(CLOCK_REALTIME, &start);

	for(i=0; i<round; i++)
	{
		ctx=ALLOC(struct async_exec_ctx, 1);
		ctx->globals=&globals;
		ctx->seq=i;
		ctx->db=db;
		clock_gettime(CLOCK_REALTIME, &ctx->start);

		if(i%3 == 0)
		{
			swarmkv_tconsume(db, tb_token, strlen(tb_token), token, simple_node_async_on_reply_cb, ctx);
		}
		if(i%3 == 1)
		{
			swarmkv_ftconsume(db, ftb_token, strlen(ftb_token), member, strlen(member), weight, token, simple_node_async_on_reply_cb, ctx);
		}
		if(i%3 == 2)
		{
			swarmkv_btconsume(db, btb_token, strlen(btb_token), member, strlen(member), token, simple_node_async_on_reply_cb, ctx);
		}
		
		curent_ts_ms=ctx->start.tv_sec*1000+ctx->start.tv_nsec/1000000;
		if(last_loop_ts_ms!=curent_ts_ms)
		{
			tmp_cnt=globals.reply_cnt;
			swarmkv_caller_loop(db, SWARMKV_LOOP_NONBLOCK, NULL);//SWARMKV_LOOP_ONCE|SWARMKV_LOOP_NONBLOCK
			loops++;
			if(tmp_cnt==globals.reply_cnt)
			{
				empty_loop++;
			}
			last_loop_ts_ms=curent_ts_ms;
		}
		if(swarmkv_caller_get_pending_commands(db)>g_node_config.pending_commands)
		{
			usleep(10);
		}
	}
	globals.cmd_inprogress=0;
	swarmkv_caller_loop(db, SWARMKV_LOOP_NO_EXIT_ON_EMPTY, NULL);
	clock_gettime(CLOCK_REALTIME, &end);
	double elapsed_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
	cmds_sec+=round*1000.0/elapsed_ms;
	printf("Async Consume %d, elapsed %lf ms, %lf cmds/s\n", round, elapsed_ms, round*1000.0/elapsed_ms);
	printf("Avg Exec Latency %lf ms, timed out %d\n",
		globals.total_exec_time_ms/round,
		globals.timedout_reply_cnt);
	printf("On fly exec %d, Loops %d, Wasted loop %d\n", globals.on_fly_reply_cnt, loops, empty_loop);

	return NULL;
}

void *start_caller_thread(void *thread_arg)
{
	struct swarmkv *db=g_node_config.db;
	int i=0;
	int key_number=100*1000, round=key_number;
	struct async_exec_globals globals;
	memset(&globals, 0, sizeof(globals));
	globals.expected_reply_cnt=round;
	globals.reply_cnt=0;
	globals.db=db;
	globals.cmd_inprogress=1;
	int tmp_cnt=0, empty_loop=0, loops=0;
	long long last_loop_ts_ms=0, curent_ts_ms=0;
	struct timespec start, end;
	struct async_exec_ctx *ctx=NULL;
	char key[256]={0};
	swarmkv_register_thread(db);

	for(i=0; i<g_node_config.cluster_node_number; i++)
	{
		batch_set_command(db, i);
	}

	wake_up_caller_thread(db);

foreach:
	clock_gettime(CLOCK_REALTIME, &start);
	int set_cmd=0, get_cmd=0;

	for(i=0; i<round; i++)
	{
		ctx=ALLOC(struct async_exec_ctx, 1);
		ctx->globals=&globals;
		ctx->seq=i;
		ctx->db=db;
		clock_gettime(CLOCK_REALTIME, &ctx->start);
		//snprintf(key, sizeof(key), "async-key-%d", i%key_number);
		snprintf(key, sizeof(key), "simple-node-key-%d-%d", g_node_config.cluster_node_id, i%key_number);
		if(i%2 == 0)
		{
			set_cmd++;
			swarmkv_set(db, key, strlen(key), "abc", 3, simple_node_async_on_reply_cb, ctx);
		}
		else
		{
			get_cmd++;
			swarmkv_get(db, key, strlen(key), simple_node_async_on_reply_cb, ctx);
		}
		curent_ts_ms=ctx->start.tv_sec*1000+ctx->start.tv_nsec/1000000;
		if(last_loop_ts_ms!=curent_ts_ms)
		{
			tmp_cnt=globals.reply_cnt;
			swarmkv_caller_loop(db, SWARMKV_LOOP_NONBLOCK, NULL);//SWARMKV_LOOP_ONCE|SWARMKV_LOOP_NONBLOCK
			loops++;
			if(tmp_cnt==globals.reply_cnt)
			{
				empty_loop++;
			}
			last_loop_ts_ms=curent_ts_ms;
		}
		if(swarmkv_caller_get_pending_commands(db)>g_node_config.pending_commands)
		{
			usleep(10);
		}

	}
	globals.cmd_inprogress=0;
	swarmkv_caller_loop(db, SWARMKV_LOOP_NO_EXIT_ON_EMPTY, NULL);
	clock_gettime(CLOCK_REALTIME, &end);
	double elapsed_ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
	cmds_sec+=round*1000.0/elapsed_ms;
	printf("Async SET %d, Get %d, elapsed %lf ms, %lf cmds/s\n", set_cmd, get_cmd, elapsed_ms, round*1000.0/elapsed_ms);
	printf("Avg Exec Latency %lf ms, timed out %d\n",
		globals.total_exec_time_ms/round,
		globals.timedout_reply_cnt);
	printf("On fly exec %d, Loops %d, Wasted loop %d\n", globals.on_fly_reply_cnt, loops, empty_loop);

	if(g_node_config.forever)
	{
		i=0;
		globals.expected_reply_cnt=round;
		globals.reply_cnt=0;
		globals.db=db;
		goto foreach;
	}

	return NULL;
}

int  batch_xxcfg_command(struct swarmkv *db, int cluster_node_id)
{
	int caller_thread_number = g_node_config.caller_thread_number;
	pthread_t threads[caller_thread_number];
	for(int i=0; i<caller_thread_number; i++)
	{
		pthread_create(&(threads[i]), NULL, start_batch_xxcfg_caller_thread, &g_node_config);
	}

	int thread_ret=0;
	for(int i=0; i<caller_thread_number; i++)
	{
		pthread_join(threads[i], (void**)&thread_ret);
	}

	return BATCH_XXXCFG_CMD;
}

int batch_set_command(struct swarmkv *db, int cluster_node_id)
{
	int i;
	long long key_number = g_node_config.key_number;
	struct timeval start, end;
	long long eplapsed_second=0;
	gettimeofday(&start, NULL);
	
	struct swarmkv_reply *reply=NULL;
	if(key_number>0)
	{
		printf("Adding %lld keys:", key_number);
		fflush(stdout);

		for(i=0; i<key_number; i++)
		{
			reply=swarmkv_command_on(db, NULL, "set simple-node-key-%d-%d simple-node-key-%d-%d", cluster_node_id, i, cluster_node_id, i);
			swarmkv_reply_free(reply);
			if(key_number > 10 && i%(key_number/10)==0)
			{
				printf(" > %lld%%", i*100/key_number);
				fflush(stdout);
			}
		}
		gettimeofday(&end, NULL);
		eplapsed_second=end.tv_sec-start.tv_sec;
		if(eplapsed_second==0){ eplapsed_second=1;}
		printf("> 100%%\nUse %lld seconds, %lld cmd/s.\n", (long long) end.tv_sec-start.tv_sec, key_number/eplapsed_second);
	}

	return BATCH_SET_CMD;
}

struct batch_cmd_spec batch_cmds[]={
	{"batch set command", batch_set_command},
	{"batch xxcfg command",  batch_xxcfg_command},
};

int batch_command_argv(struct swarmkv *db, size_t argc, char **argv)
{
	int ret=0;
	size_t i=0;	char line[256];

    if(argc==3)
    {
        snprintf(line, sizeof(line), "%s %s %s", argv[0], argv[1], argv[2]);
    }
	
	size_t n_cmd=sizeof(batch_cmds)/sizeof(struct batch_cmd_spec);
	for(i=0; i<n_cmd; i++)
	{
		if(0==strncasecmp(line, batch_cmds[i].name, strlen(batch_cmds[i].name)))
		{
			if(i==0)
			{
				swarmkv_register_thread(db);
			}
			ret = batch_cmds[i].func(db, g_node_config.cluster_node_id);
		}
	}
	return ret;
}

int main(int argc, char **argv)
{
	int ret=0;
	long long i=0;
	int worker_thread_number=2;
	int caller_thread_number=8;

	char **exec_argv=NULL;
	int exec_argc=0;

	int dryrun=0;
	g_node_config.consul_port=8500;
	strcpy(g_node_config.consul_host, "127.0.0.1");
	strncpy(g_node_config.cluster_name, "swarmkv-simple-node", sizeof(g_node_config.cluster_name));

	char cluster_announce_ip[32];
	unsigned int cluster_announce_port=0;

	char healthcheck_announce_ip[32];
	unsigned int healthcheck_announce_port=0;

	if(argc < 3) { help();}
	for (i = 1; i < argc; i++)
	{
        int lastarg = i==argc-1;
		if (!strcmp(argv[i], "-n") && !lastarg)
		{
			strncpy(g_node_config.cluster_name, argv[++i], sizeof(g_node_config.cluster_name));
		}
		else if (!strcmp(argv[i], "-h") && !lastarg)
		{
			sscanf(argv[++i], "%[^:]:%u", g_node_config.cluster_host, &g_node_config.cluster_port);
		}
		else if(!strcmp(argv[i], "-c") && !lastarg)
		{
            sscanf(argv[++i], "%[^:]:%u", g_node_config.consul_host, &g_node_config.consul_port);
		}
		else if(!strcmp(argv[i], "-t") && !lastarg)
		{
			sscanf(argv[++i], "%u", &worker_thread_number);
		}
		else if(!strcmp(argv[i], "-r") && !lastarg)
		{
			sscanf(argv[++i], "%u", &caller_thread_number);
		}
		else if(!strcmp(argv[i], "-l") && !lastarg)
		{
			sscanf(argv[++i], "%u", &g_node_config.consul_heal_check_port);
		}
		else if(!strcmp(argv[i], "-k") && !lastarg)
		{
			sscanf(argv[++i], "%lld", &g_node_config.key_number);
		}
		else if(!strcmp(argv[i], "--cluster-announce-create") && !lastarg)
		{
			sscanf(argv[++i], "%[^:]:%u", cluster_announce_ip, &cluster_announce_port);
		}
		else if(!strcmp(argv[i], "--cluster-health-announce-create") && !lastarg)
		{
           	sscanf(argv[++i], "%[^:]:%u", healthcheck_announce_ip, &healthcheck_announce_port);
		}
		else if(!strcmp(argv[i], "--node-number") && !lastarg)
		{
			sscanf(argv[++i], "%d", &g_node_config.cluster_node_number);
		}
		else if(!strcmp(argv[i], "--node-id") && !lastarg)
		{
			sscanf(argv[++i], "%d", &g_node_config.cluster_node_id);
		}
		else if(!strcmp(argv[i], "--pending-commands") && !lastarg)
		{
			sscanf(argv[++i], "%zu", &g_node_config.pending_commands);
		}
		else if(!strcmp(argv[i], "--dryrun") && !lastarg)
		{
			sscanf(argv[++i], "%d", &dryrun);
		}
		else if(!strcmp(argv[i], "--forever") && !lastarg)
		{
			sscanf(argv[++i], "%d", &g_node_config.forever);
		}
		else if(!strcmp(argv[i], "--exec") && !lastarg)
		{
			exec_argv=((char **)calloc(sizeof(char *), argc));	
			while (++i < argc && argv[i][0] != '-') 
			{
				exec_argv[exec_argc]=strdup(argv[i]);				
				exec_argc++;
			}
		}
		else
		{
			help();
		}
    }

	if(strlen(cluster_announce_ip)==0 || cluster_announce_port==0)
	{
		cluster_announce_port=g_node_config.cluster_port;
		strcpy(cluster_announce_ip, g_node_config.cluster_host);
	}

	char *err=NULL;
	const char *log_path="./swarmkv-simple-node.log";
	struct log_handle * logger=log_handle_create(log_path, 0);

	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_cluster_port(opts, g_node_config.cluster_port);
	swarmkv_options_set_bind_address(opts, g_node_config.cluster_host);
	swarmkv_options_set_consul_host(opts, g_node_config.consul_host);
	swarmkv_options_set_consul_port(opts, g_node_config.consul_port);
	swarmkv_options_set_health_check_port(opts, g_node_config.consul_heal_check_port);
	swarmkv_options_set_cluster_announce_ip(opts, cluster_announce_ip);
	swarmkv_options_set_cluster_announce_port(opts, cluster_announce_port);
	swarmkv_options_set_health_check_announce_port(opts, healthcheck_announce_port);
	swarmkv_options_set_cluster_timeout_us(opts, 1000*1000);
	swarmkv_options_set_sync_interval_us(opts, 10*1000);
	swarmkv_options_set_worker_thread_number(opts, worker_thread_number);
	swarmkv_options_set_caller_thread_number(opts, caller_thread_number);
	swarmkv_options_set_max_dispatch_interval(opts, NULL, 1000);
	swarmkv_options_set_batch_sync_enabled(opts, 1);
	if(dryrun)
	{
		swarmkv_options_set_dryrun(opts);
	}

	struct swarmkv *db=swarmkv_open(opts, g_node_config.cluster_name, &err);
	if(err)
	{
		printf("swarmkv_open instance failed: %s\n", err);
		free(err);
		err=NULL;
		return -1;
	}
	g_node_config.db=db;
	g_node_config.caller_thread_number=caller_thread_number;

	int thread_ret=0;
	pthread_t threads[caller_thread_number];
	if(exec_argc)
	{
		ret = batch_command_argv(db, exec_argc, exec_argv);
		if(ret == BATCH_XXXCFG_CMD)
		{
			goto finish;
		}
		if(ret == BATCH_SET_CMD && dryrun)
		{
			goto finish;
		}
	}

	for(int i=0; i<caller_thread_number; i++)
	{
		pthread_create(&(threads[i]), NULL, start_caller_thread, &g_node_config);
	}

	for(int i=0; i<caller_thread_number; i++)
	{
		pthread_join(threads[i], (void**)&thread_ret);
	}
	printf("Total %lf cmds/s\n", cmds_sec);

finish:
	sleep(10);
	swarmkv_close(db);
	log_handle_destroy(logger);
	return ret;
}
