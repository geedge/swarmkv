#include<stdio.h>
#include<pthread.h>
#include "gtest/gtest.h"


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
	//线程放在open里创建，后台运行，不需要等待线程结束，close负责回收
	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, swarmkv_msg_processing_thread, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return -1;
	}

	sleep(3);
	printf("******test PUT\n");
	const char* tb_name = "table0";
	const char *key = "key0";
	size_t keylen = sizeof(key);
	const char *value = "val0";
	size_t vallen = sizeof(value);
	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);
	
	sleep(1);
	char *get_val = ALLOC(char, vallen);
	size_t *get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, get_vallen, NULL);
	EXPECT_STR(get_val, "val0");
	

	if (pthread_join(tid, NULL))                 //调用pthread_join()就会阻塞在这里了    
    {
        printf("thread is not exit...\n");
	}

	close(store->sockfd);
	free(store);
	return 1;
}
