#include<stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "../src/inc_internal/swarmkv_internal.h"
#include "swarmkv.h"

int main(int argc, char **argv)
{
	//printf("%d\n",argc);
	
	const char *bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
	const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;
	struct swarmkv_store *store = NULL;
	store = ALLOC(struct swarmkv_store, 1);

	swarmkv_open(store, bootstraps, config, &err);

	printf("******test PUT******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);
	
	sleep(1);
	char *get_val = NULL;
	size_t get_vallen;
	printf("******test GET_LOCAL******\n");
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	printf("get_vallen: %ld\n", get_vallen);
	printf("%s\n",get_val);
	
	sleep(3);
	char *get_val_1 = NULL;
	size_t *get_vallen_1;
	printf("******test GET_REMOTE******\n");
	get_val_1 = swarmkv_get(store, tb_name, NULL, key, keylen, get_vallen_1, NULL);
	if(get_val_1!=NULL)
	{
		printf("%s\n",get_val_1);
	}
	printf("wait response\n");
	


	sleep(10);

	swarmkv_close(store);
	//close(store->sockfd);
	//free(store);
	return 1;
}


	/*
	struct init_infomation init_info;
	init_info.bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
	init_info.config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	init_info.err = NULL;
	*/

	/*
	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, swarmkv_msg_processing_thread, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return -1;
	}
	*/