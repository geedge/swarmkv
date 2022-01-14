#include<stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "swarmkv.h"

int main(int argc, char **argv)
{
	//test();
	//printf("%d\n",argc);
	//const char *bootstraps = "proto=udp;self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
	
	
	const char *bootstraps = "proto=udp;self=172.16.225.2:18323;peers=172.16.225.2:8323";
	const char *config = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;

	struct swarmkv_store *store = NULL;
	store = swarmkv_open(bootstraps, config, &err);
	if(store == NULL)
	{
		printf("open node failed!\n");
		return -1;
	}
	
	sleep(20);
	swarmkv_GetSlotAssign(store);
	swarmkv_close(store);
	
	/*
	const char *bootstraps_0 = "proto=udp;self=172.16.225.2:8323;peers=172.16.225.2:9323";
	const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_0 = NULL;
	struct swarmkv_store *store_0 = NULL;

	const char *bootstraps_1 = "proto=udp;self=172.16.225.2:18323;peers=172.16.225.2:8323";
	const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fc,db=0";
	char *err_1 = NULL;
	struct swarmkv_store *store_1 = NULL;

	const char *bootstraps_2 = "proto=udp;self=172.16.225.2:6323;peers=172.16.225.2:8323";
	const char *config_2 = "node_id=2;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fb,db=0";
	char *err_2 = NULL;
	struct swarmkv_store *store_2 = NULL;

	const char *bootstraps_3 = "proto=udp;self=172.16.225.2:7323;peers=172.16.225.2:8323";
	const char *config_3 = "node_id=3;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fa,db=0";
	char *err_3 = NULL;
	struct swarmkv_store *store_3 = NULL;

	store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);
	store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);
	store_2 = swarmkv_open(bootstraps_2, config_2, &err_2);
	sleep(2);
	store_3 = swarmkv_open(bootstraps_3, config_3, &err_3);

	sleep(2);

	swarmkv_GetSlotAssign(store_0);
	swarmkv_GetSlotAssign(store_1);
	swarmkv_GetSlotAssign(store_2);
	swarmkv_GetSlotAssign(store_3);

	swarmkv_close(store_0);
	swarmkv_close(store_1);
	swarmkv_close(store_2);
	swarmkv_close(store_3);
	*/
	
	return 1;
}

