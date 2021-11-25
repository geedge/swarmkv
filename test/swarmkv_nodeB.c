#include<stdio.h>
#include <pthread.h>

#include "../include/swarmkv.h"

int main(int argc, char **argv)
{
	//printf("%d\n",argc);
	const char *bootstraps = "self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
	const char *config = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;

	struct swarmkv_store *store = NULL;
	store = swarmkv_open(bootstraps, config, &err);
	if(store == NULL)
	{
		printf("open node failed!\n");
		return -1;
	}
	
	sleep(10);

	swarmkv_close(store);
	return 1;
}

