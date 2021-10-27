#include<stdio.h>

//#include "../include/swarmkv.h"
#include "swarmkv.h"

int main(int argc, char **argv)
{
	//printf("%d\n",argc);
	const char *bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
	const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;
	swarmkv_open(bootstraps, config, &err);
	/*if(err)
	{
		printf("%s", err);
		free(err);
		err=NULL;
	}*/
	/*
	if(argc == 4)
	{
		int port = atoi(argv[1]);
		//printf("%s\n",argv[2]);
		char *eth_inf = argv[2];
		int node_id = atoi(argv[3]);
		swarmkv_open(bootstraps, config, err);
	}
	else
	{
		printf("arg error\n");
	}*/

	return 1;
}

