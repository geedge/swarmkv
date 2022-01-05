#include<stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include<string.h>
#include<time.h>

#include "swarmkv.h"
#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"

void get_callback_print(const char *table, const char *key, size_t keylen, const char *value, size_t vallen, void *arg)
{
	printf("\n-----print callback result-----\n");
	printf(" table: %s\n", table);
	printf("   key: %s\n", key);
	printf("keylen: %zu\n", keylen);
	printf(" value: %s\n", value);
	printf("vallen: %zu\n", vallen);
	printf("\n");
}

int get_random_str(char* random_str, const int random_len)
{
    int i, random_num, seed_str_len;
    struct timeval tv;
    unsigned int seed_num;
    char seed_str[] = "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+@"; //随机字符串的随机字符集

    seed_str_len = strlen(seed_str);
    
    gettimeofday(&tv, NULL);
    seed_num = (unsigned int)(tv.tv_sec + tv.tv_usec); //超了unsigned int的范围也无所谓，我们要的只是不同的种子数字
    srand(seed_num);

    for(i = 0; i < random_len; i++)
    {
        random_num = rand()%seed_str_len;
        random_str[i] = seed_str[random_num];
    }

    return 0;
}

void *put_test_0(void *arg)
{
	struct swarmkv_store *store = (struct swarmkv_store *) arg;

	printf("******test GET_LOCAL speed******\n");
	const char* tb_name = "table0";
	printf("%s\n", tb_name);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	size_t get_vallen = 0;
	for(int i=500000; i<1000000; i++)
	{
		get_random_str(key_prefix, 10);
		get_random_str(val_prefix, 10);

		size_t vallen = 0;
		//snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		//snprintf(value, SWARMKV_MAX_VAL_LEN, "val-%d", i);
		snprintf(value, SWARMKV_MAX_KEY_LEN, "%s-%d", val_prefix, i);
		vallen = strlen(value);		
		//printf("* %s %s\n", key, value);
	
		swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

		//char *get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
		
		//printf("%s\n", get_val);
		//free(get_val);
		//get_val = NULL;
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix, 0, SWARMKV_MAX_KEY_LEN);
	}
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	pthread_exit(NULL);
}

void *put_test(void *arg)
{
	struct swarmkv_store *store = (struct swarmkv_store *) arg;

	printf("******test GET_LOCAL speed******\n");
	const char* tb_name = "table0";
	printf("%s\n", tb_name);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	size_t get_vallen = 0;
	for(int i=0; i<1000000; i++)
	{
		get_random_str(key_prefix, 10);
		get_random_str(val_prefix, 10);

		size_t vallen = 0;
		//snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		//snprintf(value, SWARMKV_MAX_VAL_LEN, "val-%d", i);
		snprintf(value, SWARMKV_MAX_KEY_LEN, "%s-%d", val_prefix, i);
		vallen = strlen(value);		
		//printf("* %s %s\n", key, value);
	
		swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

		//char *get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
		
		//printf("%s\n", get_val);
		//free(get_val);
		//get_val = NULL;
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix, 0, SWARMKV_MAX_KEY_LEN);
	}
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	pthread_exit(NULL);
}

int multi_thread()
{
	const char *bootstraps = "self=192.168.40.182:8323";
	const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;
	struct swarmkv_store *store = NULL;
	store = swarmkv_open(bootstraps, config, &err);
	if(store == NULL)
	{
		printf("open node failed!\n");
		return -1;
	}

	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, put_test, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return -1;
	}

	pthread_t tid_0;
	int pthread_ret_0 = pthread_create(&tid_0, NULL, put_test_0, (void*)store);
	if(pthread_ret_0 != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret_0);
        return -1;
	}

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	if ( pthread_join ( tid, NULL ) ) 
	{
		printf("error join thread.");
		abort();
  	}
	  if ( pthread_join ( tid_0, NULL ) ) 
	{
		printf("error join thread.");
		abort();
  	}
	printf("thread done! \n");
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
  	exit(0);
}

int main(int argc, char **argv)
{	
	multi_thread();
	/*
	const char *bootstraps = "self=192.168.40.182:8323";
	const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;
	struct swarmkv_store *store = NULL;
	store = swarmkv_open(bootstraps, config, &err);
	if(store == NULL)
	{
		printf("open node failed!\n");
		return -1;
	}

	const char* tb_name = "table0";
	const char *key = "key1";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	//sleep(3);

	printf("******test PUT Local******\n");
	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);
	//printf("wait response\n");
	*/

	/*
	//sleep(3);
	//printf("******test GET_REMOTE******\n");
	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	if(get_val!=NULL)
	{
		printf("%s\n",get_val);
	}
	printf("wait response\n");
	*/
	/*
	sleep(1);
	printf("******test Update_REMOTE******\n");
	const char *new_value = "val0_1";
	size_t new_vallen = strlen(value);
	swarmkv_put(store, tb_name, NULL, key, keylen, new_value, new_vallen, NULL);
	printf("wait response\n");
	*/
	
	/*
	sleep(1);
	printf("******test GET_LOCAL******\n");
	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	printf("get_vallen: %ld\n", get_vallen);
	printf("%s\n",get_val);
	free(get_val);
	get_val = NULL;
	*/
/*
	sleep(1);
	printf("******test GET_LOCAL with callback******\n");
	swarmkv_get_with_callback(store, tb_name, NULL, key, keylen, get_callback_print, NULL);


	const char *new_value = "val0_1";
	size_t new_vallen = strlen(new_value);
	printf("******test Update******\n");
	swarmkv_put(store, tb_name, NULL, key, keylen, new_value, new_vallen, NULL);

	sleep(1);
	printf("******test GET_LOCAL with callback after update******\n");
	swarmkv_get_with_callback(store, tb_name, NULL, key, keylen, get_callback_print, NULL);
*/
	/*
	sleep(5);
	printf("******test GET_REMOTE with callback******\n");
	swarmkv_get_with_callback(store, tb_name, NULL, key, keylen, get_callback_print, NULL);
	printf("wait response\n");
	*/


/*
	//sleep(3);
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	printf("******test PUT Local with callback******\n");
	swarmkv_put_with_callback(store, tb_name, NULL, key, keylen, value, vallen, get_callback_print, NULL);
*/
/*
	printf("******test PUT Remote******\n");
	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);
	printf("wait response\n");

	printf("******test PUT Remote with Callback******\n");
	swarmkv_put_with_callback(store, tb_name, NULL, key, keylen, value, vallen, get_callback_print, NULL);
	printf("wait response\n");
*/
	/*
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
	*/

	sleep(40);
	//swarmkv_GetSlotAssign(store);
	//free_store_space(store);
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