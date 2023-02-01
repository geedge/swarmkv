#include "swarmkv/swarmkv.h"
#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <time.h>

#include "log.h"

#define FOREVER for(;;)

static void help()
{
	fprintf(stderr, "Welcome to Swarmkv Node\n");
	fprintf(stderr,
		"swarmkv_simple_node <-n| -h | -p |-t> arg\n"
		"Usage:\n"
		"  -n <Cluster Name>\n"
		"  -h <Ip:port>\n"
		"  -c <Consul Ip:Consul port>\n"
		"  -a <Cluster Announce ip:Cluster Announce Port>\n"
		"  -b <Health Check Announce Ip:Health Check Announce Port>\n"
		"  -t <Thread Number>\n"
		"  -k <Key Number>\n"
		"  -l <Consul Heal Check Port>\n");
	    exit(1);
}

int main(int argc, char ** argv)
{
	int ret=0;
	long long i=0;
	char host[32]={0};
	char consul_host[32]={0};
	long long key_number=0;
	int worker_thread_number=1;
	unsigned int consul_port=8500;
	unsigned int cluster_port;
	unsigned int consul_heal_check_port;
	char cluster_name[256];

	char cluster_announce_ip[32];
	unsigned int cluster_announce_port;

	char healthcheck_announce_ip[32];
	unsigned int healthcheck_announce_port;

	if(argc < 3) { help();}
	strcpy(consul_host, "127.0.0.1");
	strncpy(cluster_name, "swarmkv-simple-node", sizeof(cluster_name));
	for (i = 1; i < argc; i++)
	{
        int lastarg = i==argc-1;
		if (!strcmp(argv[i], "-n") && !lastarg)
		{
			 strncpy(cluster_name, argv[++i], sizeof(cluster_name));
		}
		else if (!strcmp(argv[i], "-h") && !lastarg)
		{
			sscanf(argv[++i], "%[^:]:%u", host, &cluster_port);
		}
		else if(!strcmp(argv[i], "-c") && !lastarg)
		{
           	sscanf(argv[++i], "%[^:]:%u", consul_host, &consul_port);
		}
		else if(!strcmp(argv[i], "-a") && !lastarg)
		{
           	sscanf(argv[++i], "%[^:]:%u", cluster_announce_ip, &cluster_announce_port);
		}
		else if(!strcmp(argv[i], "-b") && !lastarg)
		{
           	sscanf(argv[++i], "%[^:]:%u", healthcheck_announce_ip, &healthcheck_announce_port);
		}
		else if(!strcmp(argv[i], "-t") && !lastarg)
		{
			sscanf(argv[++i], "%u", &worker_thread_number);
		}
		else if(!strcmp(argv[i], "-k") && !lastarg)
		{
			sscanf(argv[++i], "%lld", &key_number);
		}
		else if(!strcmp(argv[i], "-l") && !lastarg)
		{
			sscanf(argv[++i], "%u", &consul_heal_check_port);
		}
		else
		{
			help();
		}
    }

	char *err=NULL;

	const char *log_path="./swarmkv-simple-node.log";
	struct log_handle * logger=log_handle_create(log_path, 0);

	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_logger(opts, logger);
	swarmkv_options_set_cluster_port(opts, cluster_port);
	swarmkv_options_set_bind_address(opts, host);
	swarmkv_options_set_consul_host(opts, consul_host);
	swarmkv_options_set_consul_port(opts, consul_port);
	swarmkv_options_set_health_check_port(opts, consul_heal_check_port);

	swarmkv_options_set_cluster_announce_ip(opts, cluster_announce_ip);
	swarmkv_options_set_cluster_announce_port(opts, cluster_announce_port);
	swarmkv_options_set_health_check_announce_port(opts, healthcheck_announce_port);

	swarmkv_options_set_worker_thread_number(opts, worker_thread_number);
	swarmkv_options_set_cluster_timeout_us(opts, 500000);
	swarmkv_options_set_sync_interval_us(opts, 500);
	//swarmkv_options_set_disable_run_for_leader(opts);
	struct swarmkv *db=swarmkv_open(opts, cluster_name, &err);
	if(err)
	{
		printf("swarmkv_open instance failed: %s\n", err);
		free(err);
		err=NULL;
		return -1;
	}
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
			reply=swarmkv_command_on(db, NULL, "TCFG tb-%d 100000 10000", i, i);
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
	unsigned int round=0;
	FOREVER
	{
		reply=swarmkv_command(db, "tconsume tb-%lld 1000", round%key_number);
		swarmkv_reply_free(reply);
		round++;
	};
	swarmkv_close(db);
	return ret;
}

