#include<stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include<string.h>
#include<time.h>
#include<MESA/field_stat2.h>

#include "swarmkv.h"
#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"



#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h> 
#include <net/if.h>  
#include <sys/ioctl.h> 
char* get_localip(const char *eth_inf)
{
    int sd;
    //struct sockaddr_in sin;
    struct ifreq ifr;
 
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sd)
    {
        printf("socket error: %s\n", strerror(errno));
        return NULL;
    }
    strncpy(ifr.ifr_name, eth_inf, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    if (ioctl(sd, SIOCGIFADDR, &ifr) < 0)
    {
        printf("ioctl error: %s\n", strerror(errno));
        close(sd);
        return NULL;
    }
    //printf("interfac: %s, ip: %s\n", eth_inf, inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr)); 
	char* local_ip = inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
    close(sd);
	//printf("local_ip: %s\n",local_ip);
    return local_ip;
}

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

void *put_test(void *arg)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 1000;

	struct swarmkv_store *store = (struct swarmkv_store *) arg;
	//char* tb_name = ALLOC(char, 32);
	const char* tb_name = "table0";
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);
			
	//get_random_str(key_prefix, key_prefix_len);
	//get_random_str(val_prefix, val_prefix_len);

	//size_t get_vallen = 0;
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<1000000; i++)
	{
		get_random_str(key_prefix, key_prefix_len);
		get_random_str(val_prefix, val_prefix_len);
		size_t vallen = 0;
		//snprintf(tb_name, 32, "table%d", i%100);
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix, i);
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
		memset(val_prefix, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	pthread_exit(NULL);
}

int single_thread()
{
	char *local_ip = get_localip("eth0");
	printf("%s\n", local_ip);
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
	printf("============= Single thread =============\n");
	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, put_test, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return -1;
	}

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	if ( pthread_join(tid, NULL) ) 
	{
		printf("error join thread.");
		abort();
  	}
	printf("thread done! \n");
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("total_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
  	exit(0);

}

void *put_test_0(void *arg)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 1000;

	struct swarmkv_store *store = (struct swarmkv_store *) arg;
	//char* tb_name = ALLOC(char, 32);
	const char* tb_name = "table0";
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix_0 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix_0 = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	//get_random_str(key_prefix_0, key_prefix_len);
	//get_random_str(val_prefix_0, val_prefix_len);

	//size_t get_vallen = 0;
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<340000; i++)
	{
		get_random_str(key_prefix_0, key_prefix_len);
		get_random_str(val_prefix_0, val_prefix_len);

		size_t vallen = 0;
		//snprintf(tb_name, 32, "table%d", i%100);
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix_0, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix_0, i);
		vallen = strlen(value);		
		//printf("* %s %s\n", key, value);

		swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

		//char *get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
		//printf("%s\n", get_val);
		//free(get_val);
		//get_val = NULL;
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix_0, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix_0, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time_0 : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	pthread_exit(NULL);
}

void *put_test_1(void *arg)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 1000;

	struct swarmkv_store *store = (struct swarmkv_store *) arg;
	const char* tb_name = "table1";
	//char* tb_name = ALLOC(char, 32);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix_1 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix_1 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	//size_t get_vallen = 0;
	//get_random_str(key_prefix_1, key_prefix_len);
	//get_random_str(val_prefix_1, val_prefix_len);

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<330000; i++)
	{
		get_random_str(key_prefix_1, key_prefix_len);
		get_random_str(val_prefix_1, val_prefix_len);
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix_1, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix_1, i);
		vallen = strlen(value);		
		//printf("* %s %s\n", key, value);
	
		swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

		//char *get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
		//printf("%s\n", get_val);
		//free(get_val);
		//get_val = NULL;
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix_1, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix_1, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time_1 : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	pthread_exit(NULL);
}

void *put_test_2(void *arg)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 1000;

	struct swarmkv_store *store = (struct swarmkv_store *) arg;
	const char* tb_name = "table2";
	//char* tb_name = ALLOC(char, 32);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix_2 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix_2 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	//size_t get_vallen = 0;
	//get_random_str(key_prefix_2, key_prefix_len);
	//get_random_str(val_prefix_2, val_prefix_len);
	
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<330000; i++)
	{
		get_random_str(key_prefix_2, key_prefix_len);
		get_random_str(val_prefix_2, val_prefix_len);
		size_t vallen = 0;
		
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix_2, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix_2, i);
		vallen = strlen(value);		
		//printf("* %s %s\n", key, value);
	
		swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

		//char *get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
		
		//printf("%s\n", get_val);
		//free(get_val);
		//get_val = NULL;
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix_2, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix_2, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time_2 : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
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

	printf("============= Multi thread =============\n");
	pthread_t tid_0;
	int pthread_ret = pthread_create(&tid_0, NULL, put_test_0, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return -1;
	}

	pthread_t tid_1;
	int pthread_ret_0 = pthread_create(&tid_1, NULL, put_test_1, (void*)store);
	if(pthread_ret_0 != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret_0);
        return -1;
	}

    pthread_t tid_2;
	int pthread_ret_1 = pthread_create(&tid_2, NULL, put_test_2, (void*)store);
	if(pthread_ret_1 != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret_0);
        return -1;
	}

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	if ( pthread_join(tid_0, NULL) ) 
	{
		printf("error join thread.");
		abort();
  	}
	if ( pthread_join(tid_1, NULL) ) 
	{
		printf("error join thread.");
		abort();
  	}
    if ( pthread_join(tid_2, NULL) ) 
	{
		printf("error join thread.");
		abort();
  	}
	printf("all thread done! \n");
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("total_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
  	exit(0);
}



int main(int argc, char **argv)
{	
	//multi_thread();
	single_thread();
	return 1;
}