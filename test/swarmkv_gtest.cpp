#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include <gtest/gtest.h>
#include<time.h>
#include<MESA/field_stat2.h>
#include "swarmkv.h"
#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"

//判断key在哪里，用于测试migrate成功与否
//swarmkv_getkeyopt(kv, key, SWMKV_OPT_KEY_LOCATION, *value);
//EXPECT_EQ (value, LOCAL_KEY);

const char* host_ip = "192.168.40.182";

/*————————————————————————————————————辅助函数————————————————————————————————————————*/
/*————————————————————————————————————辅助函数————————————————————————————————————————*/
/*————————————————————————————————————辅助函数————————————————————————————————————————*/
void callback_print(const char *table, const char *key, size_t keylen, const char *value, size_t vallen, void *arg)
{
	printf("\n-----print callback result-----\n");
	printf(" table: %s\n", table);
	printf("   key: %s\n", key);
	printf("keylen: %zu\n", keylen);
	printf(" value: %s\n", value);
	printf("vallen: %zu\n", vallen);
	
	EXPECT_STREQ(value,"val0"); 
	printf("\n");
}

void callback_with_arg(const char *table, const char *key, size_t keylen, const char *value, size_t vallen, void *arg)
{
	printf(" value: %s\n", value);
	memcpy(arg, value, vallen);
}

int get_slot_table(int NID[], int node_num, struct swarmkv_slot *new_slots)
{
	struct conhash* ch = NULL;
	ch = (struct conhash*)swarmkv_conhash_create(NID, node_num, SWARMKV_POINTS_PER_NODE_ON_CHR);
	int i=0;
	uint32_t j = 0;
	int point_num = node_num * SWARMKV_POINTS_PER_NODE_ON_CHR;
	for(i=0; i<point_num; i++)
	{
		if( i == 0)
		{
			for(j = 0; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;	
			}
			for(j=ch->point[point_num-1].point_val+1; j<SWARMKV_DEFAULT_SLOT_NUM; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
		else
		{
			for(j = ch->point[i-1].point_val+1; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
	}
	
	printf("\n=======================================\n");
	for(int k=0; k<SWARMKV_DEFAULT_SLOT_NUM; k++)
	{
		printf("%d:%d ", new_slots[k].slot_id, new_slots[k].owner_node_id);
	}
	printf("\n=======================================\n");
	
	conhash_destroy(ch);
	return 1;
}

int check_slot_table_state(struct swarmkv_store *store, int NID[], int node_num)
{
	struct swarmkv_slot *new_slots = ALLOC(struct swarmkv_slot, SWARMKV_DEFAULT_SLOT_NUM);
	get_slot_table(NID, node_num, new_slots);
	
	for(int i=0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
	{
		int exp_owner_id = new_slots[i].owner_node_id;
		int store_owner_id = store->slots[i].owner_node_id;
		EXPECT_EQ(store_owner_id, exp_owner_id);
	}
	free(new_slots);
	new_slots = NULL;
	return 1;
}

char *swarmkv_get_cache(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, size_t *vallen)
{
	int slot_id = caculate_key_slot_id(key, keylen);
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	char *value = NULL;
	HASH_FIND_STR(store->slots[slot_id].table_hash, tb_name, table);
	if(table != NULL)
	{
		pthread_rwlock_rdlock(&table->rwlock);
		HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
		if(kv == NULL)
		{
			//printf("I don't find the key in my cache!\n");
		}
		else
		{
			clock_t elapsed_time = clock();
			if(elapsed_time - kv->cached_time < SWARMKV_CACHE_EXPIREATION_PERIORD)
			{
				value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
				memcpy(value, kv->value, kv->vallen);
				*vallen = kv->vallen;
			}
			else
			{
				printf("expiration\n");
				del_cache_kv(store, tb_name, key, keylen);
			}
		}
		pthread_rwlock_unlock(&table->rwlock);
	}
	else
	{
		//printf("I don't have the table in my cache!\n");
	}
	return value;
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

/*————————————————————————————————————测试用例————————————————————————————————————————*/
/*————————————————————————————————————测试用例————————————————————————————————————————*/
/*————————————————————————————————————测试用例————————————————————————————————————————*/

class SwarmkvLocalFunctionTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	virtual void SetUp()
	{
		char *bootstraps = ALLOC(char, 128); 
		snprintf(bootstraps, 128, "self=%s:8323", host_ip);
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store_0 = swarmkv_open(bootstraps, config, &err);
	}
	virtual void TearDown()
	{
		free_store_space(store_0);
	}
};


//测试本地put一个kv并get_with_callback，不带参数的callback函数
TEST_F(SwarmkvLocalFunctionTest, GetCallbackLocal)
{
	printf("******test GET_LOCAL with callback******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	swarmkv_get_with_callback(store_0, tb_name, NULL, key, keylen, callback_print, NULL);
}


//测试本地put一个kv并get_with_callback,带参数的callback函数
TEST_F(SwarmkvLocalFunctionTest, GetCallbackwithArgLocal)
{
	printf("******test GET_LOCAL with callback(Arg)******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	const char* tb_name_1 = "table1";
	const char *key_1 = "key3";
	size_t keylen_1 = strlen(key_1);
	const char *value_1 = "val1";
	size_t vallen_1 = strlen(value_1);

	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
	swarmkv_put(store_0, tb_name_1, NULL, key_1, keylen_1, value_1, vallen_1, NULL);

	char *callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	swarmkv_get_with_callback(store_0, tb_name, NULL, key, keylen, callback_with_arg, callback_value);

	char *callback_value_1 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	swarmkv_get_with_callback(store_0, tb_name_1, NULL, key_1, keylen_1, callback_with_arg, callback_value_1);

	printf("%s\n", callback_value);
	const char *exp0 = "val0";
	EXPECT_EQ(*callback_value, *exp0); 

	printf("%s\n", callback_value_1);
	const char *exp1 = "val1";
	EXPECT_EQ(*callback_value_1, *exp1); 

	free(callback_value);
	callback_value = NULL;
	free(callback_value_1);
	callback_value_1 = NULL;
}


//测试本地put_with_callback，不含参数
TEST_F(SwarmkvLocalFunctionTest, PUTCallbackLocal)
{
	printf("******test PUT_LOCAL with callback******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put_with_callback(store_0, tb_name, NULL, key, keylen, value, vallen, callback_print, NULL);

}

//测试本地put_with_callback，含参数
TEST_F(SwarmkvLocalFunctionTest, PUTCallbackwithArgLocal)
{
	printf("******test PUT_LOCAL with callback(Arg)******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	char *callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	swarmkv_put_with_callback(store_0, tb_name, NULL, key, keylen, value, vallen, callback_with_arg, callback_value);

	printf("%s\n", callback_value);
	const char *exp0 = "val0";
	EXPECT_EQ(*callback_value, *exp0); 
	free(callback_value);
	callback_value = NULL;
}

//测试本地对于同一个key，更新value
TEST_F(SwarmkvLocalFunctionTest, UpdateLocal)
{
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);
	
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);  
	free(get_val);
	get_val = NULL;

	const char *new_value = "val1";
	size_t new_vallen = strlen(new_value);
	swarmkv_put(store_0, tb_name, NULL, key, keylen, new_value, new_vallen, NULL);
	get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);
	
	const char *exp1 = "val1";
	EXPECT_EQ(*get_val, *exp1);
	free(get_val);
	get_val = NULL;
}


///批量操作测试速度
class SwarmkvLocalSpeedTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	virtual void SetUp()
	{
		char *bootstraps = ALLOC(char, 128); 
		snprintf(bootstraps, 128, "self=%s:8323", host_ip);
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store_0 = swarmkv_open(bootstraps, config, &err);
	}
	virtual void TearDown()
	{
		free_store_space(store_0);
	}
};

/*
//测试本地put kv
TEST_F(SwarmkvLocalSpeedTest, PutLocalSmallSpeed)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 1000;

	screen_stat_handle_t handle = NULL;
	handle = FS_create_handle();
	int FS_value = 1;
	FS_set_para(handle, CREATE_THREAD, &FS_value, sizeof(FS_value));
	FS_value = 1;
	FS_set_para(handle, STAT_CYCLE, &FS_value, sizeof(FS_value));
	int histogram_ids = FS_register_histogram(handle, FS_CALC_CURRENT, "op_delay", 1, 100000, 3);
	struct timespec start;

	printf("******test PUT_LOCAL speed******\n");
	const char* tb_name = "table0";
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	get_random_str(key_prefix, key_prefix_len);
	get_random_str(val_prefix, val_prefix_len);
	
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	FS_start(handle);
	for(int i=0; i<1000000; i++)
	{
		
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix, i);
		size_t vallen = strlen(value);	
		//printf("%zu %s\n", keylen, key);	
		//printf("%zu %s\n", vallen, value);
		
		record_time_start(&start);
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
		long eplapses=record_time_elapse_us(&start);
		FS_operate(handle, histogram_ids, 0, FS_OP_SET, eplapses);

		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		//memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
		//memset(val_prefix, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("put_use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	sleep(3);
	FS_stop(&handle);
	FS_library_destroy();
}
*/


//测试本地put kv并get
TEST_F(SwarmkvLocalSpeedTest, GetLocalSmallSpeed)
{
	//设置kv大小
	int key_prefix_len = 16;
	int val_prefix_len = 512;

	screen_stat_handle_t handle = NULL;
	handle = FS_create_handle();
	int FS_value = 1;
	FS_set_para(handle, CREATE_THREAD, &FS_value, sizeof(FS_value));
	FS_value = 1;
	FS_set_para(handle, STAT_CYCLE, &FS_value, sizeof(FS_value));
	int histogram_ids = FS_register_histogram(handle, FS_CALC_CURRENT, "op_delay", 1, 100000, 3);
	struct timespec start;

	printf("******test GET_LOCAL speed******\n");
	const char* tb_name = "table0";
	printf("%s\n", tb_name);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	get_random_str(key_prefix, key_prefix_len);
	get_random_str(val_prefix, val_prefix_len);

	size_t get_vallen = 0;
	char *get_val = NULL;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	FS_start(handle);
	for(int i=0; i<1000000; i++)
	{
		
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix, i);
		size_t vallen = strlen(value);	
		
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

		record_time_start(&start);
		get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);
		long eplapses=record_time_elapse_us(&start);
		FS_operate(handle, histogram_ids, 0, FS_OP_SET, eplapses);
		
		//printf("%s\n", get_val);
		snprintf(exp_value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix, i);
		EXPECT_EQ(*get_val, *exp_value); 

		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		//memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
		//memset(val_prefix, 0, SWARMKV_MAX_VAL_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 
		free(get_val);
		get_val = NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("get_use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	free(exp_value);
	exp_value = NULL;
	free(key_prefix);
	key_prefix = NULL;
	free(val_prefix);
	val_prefix = NULL;

	sleep(3);
	FS_stop(&handle);	
	FS_library_destroy();
}


/*
//测试本地putkv 256bytes/256bytes
TEST_F(SwarmkvLocalSpeedTest, PutLocalMidSpeed)
{
	screen_stat_handle_t handle = NULL;
	handle = FS_create_handle();
	int FS_value = 1;
	FS_set_para(handle, CREATE_THREAD, &FS_value, sizeof(FS_value));
	FS_value = 2;
	FS_set_para(handle, STAT_CYCLE, &FS_value, sizeof(FS_value));
	int histogram_ids = FS_register_histogram(handle, FS_CALC_CURRENT, "op_delay", 1, 100000, 3);
	struct timespec start;

	printf("******test PUT_Local and GET_LOCAL speed******\n");
	const char* tb_name = "table0";
	printf("%s\n", tb_name);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	//const char *key_prefix = "djhienwhfbhvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr76 calkdpokwed ndjehiu3ehbd jbhvgnfdjrjfinjfnjbyugfhbedkey>dfj*#2djhienwvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr7684bjbh calkdpokwed ndjehiu3ehbd jbhvdfdgnfdjrjfinjfnjbyugfhbedkeydjhienwhfbhvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr7684bjbh calkdpokwed ndjehiu3ehbd jbhvgnfdjrjfinjfnjbyugfhbedkey>dfj*#2djhienwvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr7684bjbh calkdpokwed ndjehiu3ehbd jbhvdfdgnfdjrjfinjfnjbyugfhbedkey";
	//const char *val_prefix = "hewhioncyergvybjwnfriovrkncjdinsxnwjheuddnjehjnefiubhrbfh83 this is amide duerfmksjeiwbbfy duehdkjfkfrm jehf9487hewhioncyergvybjwnfriovrdinsxnwjheuddnjehjnefiubhrbfh83274868 this is amide duerfmksjeiwbbfy duehdkjfkfrmnusgwuhhudebednusgwuhhudebeddjhienwhfbhvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr7684bjbh calkdpokwed ndjehiu3ehbd jbhvgnfdjrjfinjfnjbyugfhbedkey>dfj*#2djhienwvfhvrnxj 3276998 n0sjcbjh ebncbdjh bandknkeahuwdyr7684bjbh calkdpokwed ndjehiu3ehbd jbhvdfdgnfdjrjfinjfnjbyugfhbedkeyvalue";
	//printf("%zu\n", strlen(key_prefix));
	//printf("%zu\n", strlen(val_prefix));
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	FS_start(handle);
	for(int i=0; i<10; i++)
	{
		size_t vallen = 0;
 
   		get_random_str(key_prefix, 10);
		get_random_str(val_prefix, 10);
		
		//snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		//snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix,i);

		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "val-%d",i);

		printf("* %s\n", key);
		vallen = strlen(value);
		
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
		
		size_t get_vallen = 0;
		char *get_val;
		record_time_start(&start);
		get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);
		printf("get_val: %s\n", get_val);
		long eplapses=record_time_elapse_us(&start);
		FS_operate(handle, histogram_ids, 0, FS_OP_SET, eplapses);
		free(get_val);
		get_val = NULL;

		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	sleep(3);
	FS_stop(&handle);
	free(key);
	key = NULL;
	free(value);
	value = NULL;
}
*/


//三个节点，环回
class SwarmkvRemoteTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[3];
	virtual void SetUp()
	{
		const char *bootstraps_0 = "self=192.168.40.182:1323";
		const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err_0 = NULL;
		store[0] = swarmkv_open(bootstraps_0, config_0, &err_0);
		for(int i=1; i<3; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			snprintf(bootstraps, 128, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			snprintf(config, 128, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
		}
	}
	virtual void TearDown()
	{
		pthread_join(store[0]->tid, NULL);
		free_store_space(store[0]);
		pthread_join(store[1]->tid, NULL);
		free_store_space(store[1]);
		pthread_join(store[2]->tid, NULL);
		free_store_space(store[2]);
	}
};

//测试远程put_with_callback
TEST_F(SwarmkvRemoteTest, PUTCallbackwithArgLocal)
{
	int node_num = 3;
	while(1)
	{
		int flag = 0;
		for(int i=0; i<node_num; i++)
		{
			if(store[i]->meet_flag == 1)
			{
				flag = 1;

				break;
			}
		}
		if(flag == 0)
		{
			break;
		}
	}
	sleep(5);
	int NID[3] = {0,1,2};
	for(int i=0; i<node_num; i++)
	{
		check_slot_table_state(store[i], NID, node_num);
	}

	printf("******test PUT_Remote with callback(Arg)******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	//int slot_id = caculate_key_slot_id(key, keylen);
	//printf("slot_id: %d\n", slot_id);

	char *callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	swarmkv_put_with_callback(store[0], tb_name, NULL, key, keylen, value, vallen, callback_with_arg, callback_value);

	const char *exp0 = "val0";
	while(1)
	{
		if(strcmp(callback_value, exp0) == 0)
		{
			break;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	printf("%s\n", callback_value);
	EXPECT_EQ(*callback_value, *exp0); 
	free(callback_value);
	callback_value = NULL;
}

//测试本地缓存和invalidate
TEST_F(SwarmkvRemoteTest, CacheandInvalidate)
{
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 )
		{
			break;
		}
	}
	sleep(5);
	int NID[3] = {0,1,2};
	int node_num = 3;
	for(int i=0; i<node_num; i++)
	{
		check_slot_table_state(store[i], NID, node_num);
	}

	printf("******test get remote & cache accuracy and invalidate speed******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	
	//put
	char *put_callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	swarmkv_put_with_callback(store[0], tb_name, NULL, key, keylen, value, vallen, callback_with_arg, put_callback_value);

	//get
	const char *exp_val = "val0";
	char *get_callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	swarmkv_get_with_callback(store[0], tb_name, NULL, key, keylen, callback_with_arg, get_callback_value);
	while(1)
	{
		if(strcmp(get_callback_value, exp_val) == 0)
		{
			break;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("get_use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	printf("get value: %s\n", get_callback_value);

	//check cache
	char *cache_value = NULL;
	size_t get_vallen;
	cache_value = swarmkv_get_cache(store[0], tb_name, key, keylen, &get_vallen);
	if(cache_value!=NULL)
	{
		printf("cache_value: %s\n", cache_value);
		EXPECT_EQ(*cache_value, *exp_val); 
		free(cache_value);
		cache_value = NULL;
	}

	//update
	const char *new_value = "valnew";
	size_t new_vallen = strlen(new_value);
	swarmkv_put_with_callback(store[1], tb_name, NULL, key, keylen, new_value, new_vallen, callback_with_arg, put_callback_value);

	//check cache invalidate
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	int cnt = 0;
	while(1)
	{
		cache_value = swarmkv_get_cache(store[0], tb_name, key, keylen, &get_vallen);
		if(cache_value == NULL)
		{
			break;
		}
		cnt++;
		free(cache_value);
		cache_value = NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	printf("cnt: %d\n", cnt);

	free(get_callback_value);
	get_callback_value = NULL;
	free(put_callback_value);
	put_callback_value = NULL;
}


class SwarmkvClusterInitTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[16];
	virtual void SetUp()
	{
		const char *bootstraps0 = "self=192.168.40.182:1323";
		const char *config0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err0 = NULL;
		store[0] = swarmkv_open(bootstraps0, config0, &err0);
		for(int i=1; i<16; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			snprintf(bootstraps, 128, "self=%s:%d;peers=%s:1323", host_ip, i+1323, host_ip);
			snprintf(config, 128, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
			/*
			while(store[i]->meet_flag)
			{
				//sleep(1);
			}
			*/
		}
	}
	virtual void TearDown()
	{		
		printf("test end\n");
	}
};


//测试初始化N个节点的时间
TEST_F(SwarmkvClusterInitTest, EmptyStartSpeed)
{
	printf("******test Start Result******\n");
	int node_num = 16;
	while(1)
	{
		int flag = 0;
		for(int i=0; i<node_num; i++)
		{
			if(store[i]->meet_flag == 1)
			{
				flag = 1;

				break;
			}
		}
		if(flag == 0)
		{
			break;
		}
	}
	printf("*ok\n");
}


/*
//测试在十六个节点集群中随机读取的时间（本地环回）
TEST_F(SwarmkvClusterInitTest, RadomGetSpeed)
{
	printf("******test Start Result******\n");
	int node_num = 50;
	while(1)
	{
		int flag = 0;
		for(int i=0; i<node_num; i++)
		{
			if(store[i]->meet_flag == 1)
			{
				flag = 1;

				break;
			}
		}
		if(flag == 0)
		{
			break;
		}
	}
	printf("*ok\n");
	
	sleep(20);

	screen_stat_handle_t handle = NULL;
	handle = FS_create_handle();
	int FS_value = 1;
	FS_set_para(handle, CREATE_THREAD, &FS_value, sizeof(FS_value));
	FS_value = 2;
	FS_set_para(handle, STAT_CYCLE, &FS_value, sizeof(FS_value));
	int histogram_ids = FS_register_histogram(handle, FS_CALC_CURRENT, "op_delay", 1, 100000, 3);
	struct timespec start;
	
	//put
	printf("start puts\n");
	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	FS_start(handle);
	for(int i=0; i<100000; i++)
	{
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		//printf("* %s %s\n", key, value);
		size_t vallen = strlen(value);

		record_time_start(&start);
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
		long eplapses=record_time_elapse_us(&start);
		FS_operate(handle, histogram_ids, 0, FS_OP_SET, eplapses);

		usleep(1000);

		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	printf("put end\n");
	sleep(5);
	FS_stop(&handle);
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	sleep(200);

	
	//get
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	printf("start time :\n");
	for(int j=0; j<10000; j++)
	{
		int tmp = rand()%1000000;
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", tmp);
		size_t keylen = strlen(get_key);
		//printf("* %s %zu\n", get_key, keylen);
		get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		while(get_val == NULL)
		{
			get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
		}
		if(get_val != NULL)
		{
			snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", tmp);
			EXPECT_EQ(*get_val, *exp_value); 
			//printf("find: %s\n", get_val);
			free(get_val);
			get_val = NULL;
		}
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 	
	}
	free(get_val);
	get_val = NULL;
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
	
}
*/


/*
//写入10W个kv,然后远程随机读取一个
TEST_F(SwarmkvClusterInitTest, RadomPutandGetOne)
{
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	printf("******test Start Result******\n");
	sleep(10);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}

	//put
	printf("start puts\n");
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<100000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value%d", i);
		printf("* %s %s\n", key, value);
		vallen = strlen(value);
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	sleep(20);

	//get one
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	printf("start time :\n");

	int tmp = rand()%100000;
	snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key%d", tmp);
	size_t keylen = strlen(get_key);
	get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
	while(get_val == NULL)
	{
		get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	
	snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", tmp);
	EXPECT_EQ(*get_val, *exp_value); 
	//printf("find: %s\n", get_val);
	free(get_val);
	get_val = NULL;
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
}
*/

/*
//初始化状态：一个初始节点，存储了N个key-value,然后扩展为16个
class SwarmkvClusterScaleTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[16];
	virtual void SetUp()
	{
		const char *bootstraps = "self=192.168.40.182:1323";
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store[0] = swarmkv_open(bootstraps, config, &err);

		struct timespec start_time = {0, 0};
		struct timespec end_time = {0, 0};
		clock_gettime(CLOCK_MONOTONIC, &start_time);
		const char* tb_name = "table0";
		char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
		char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
		for(int i=0; i<100000; i++)
		{
			size_t vallen = 0;
			snprintf(key, SWARMKV_MAX_KEY_LEN, "key%d", i);
			size_t keylen = strlen(key);
			snprintf(value, SWARMKV_MAX_VAL_LEN, "value%d", i);
			//printf("* %s %s\n", key, value);
			vallen = strlen(value);
			swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
			memset(key, 0, SWARMKV_MAX_KEY_LEN);
			memset(value, 0, SWARMKV_MAX_VAL_LEN);
		}
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
		free(key);
		key = NULL;
		free(value);
		value = NULL;

		for(int i=1; i<16; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			snprintf(bootstraps, 128, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			snprintf(config, 128, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
		}
	}
	virtual void TearDown()
	{
		//free_store_space(store[0]);
		printf("end\n");
	}
};

//测试从一个节点扩展为16个，所有slot迁移完毕所需时间
TEST_F(SwarmkvClusterScaleTest, StartSpeedwithKV)
{
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	sleep(10);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}
}

//测试16个节点，512个槽的情况下，随机读取kv的情况
TEST_F(SwarmkvClusterScaleTest, CheckKVMigrate)
{
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	sleep(10);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}
	
	const char* tb_name = "table0";
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int j=0; j<10000; j++)
	{
		int tmp = rand()%100000;
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key%d", tmp);
		size_t keylen = strlen(get_key);
		//printf("* %s %zu\n", get_key, keylen);
		get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		while(get_val == NULL)
		{
			get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
		}
		if(get_val != NULL)
		{
			snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", j);
			EXPECT_EQ(*get_val, *exp_value); 
			//printf("find: %s\n", get_val);
			free(get_val);
			get_val = NULL;
		}
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 	
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(get_val);
	get_val = NULL;
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
}
*/

/*
class SwarmkvClusterTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[16];
	virtual void SetUp()
	{
		for(int i=0; i<16; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			if(i==0)
			{
				snprintf(bootstraps, 128, "self=%s:%d323", host_ip, i+1);
			}
			else
			{
				snprintf(bootstraps, 128, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			}
			snprintf(config, 128, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
			free(bootstraps);
			bootstraps = NULL;
			free(config);
			config = NULL;
		}
	}
	virtual void TearDown()
	{		
		printf("test end\n");
	}
};


TEST_F(SwarmkvClusterTest, MeetandMigrateTest)
{
	printf("******test Start Result******\n");
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	sleep(10);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}

	sleep(30);

	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<100000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		//printf("* %s %s\n", key, value);
		vallen = strlen(value);
		
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);

		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		usleep(50);
	}
	printf("put end\n");
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	sleep(20);
	printf("sleep end\n");

	screen_stat_handle_t handle = NULL;
	handle = FS_create_handle();
	int FS_value = 1;
	FS_set_para(handle, CREATE_THREAD, &FS_value, sizeof(FS_value));
	FS_value = 2;
	FS_set_para(handle, STAT_CYCLE, &FS_value, sizeof(FS_value));
	int histogram_ids = FS_register_histogram(handle, FS_CALC_CURRENT, "op_delay", 1, 100000, 3);
	struct timespec start;

	size_t get_vallen = 0;
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	int cnt = 0;
	FS_start(handle);
	printf("start get\n");
	for(int j=0; j<100000; j++)
	{
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", j);
		size_t keylen = strlen(get_key);

		record_time_start(&start);
		get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		while(get_val == NULL)
		{
			get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
		}
		long eplapses = record_time_elapse_us(&start);
		FS_operate(handle, histogram_ids, 0, FS_OP_SET, eplapses);
		
		if(get_val != NULL)
		{
			if (strlen(get_val) == 0)
			{
				cnt++;
			}
			snprintf(exp_value, SWARMKV_MAX_VAL_LEN, "value-%d", j);
			EXPECT_EQ(*get_val, *exp_value); 
			free(get_val);
			get_val = NULL;
		}
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 
		free(get_val);
		get_val = NULL;
	}
	sleep(3);
	FS_stop(&handle);
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
	printf("%d\n", cnt);

	sleep(512);
}
*/

/*
//测试出错，有key-value在迁移的过程中会丢失，不明白为什么？
TEST_F(SwarmkvClusterTest, GetafterMigratingTest)
{
	printf("******test Start Result******\n");
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	sleep(10);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<1000000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		//printf("* %s %s\n", key, value);
		vallen = strlen(value);
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("\nput_use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	sleep(40);

	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int j=0; j<10000; j++)
	{
		//int tmp = rand()%100000;
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", j*10);
		size_t keylen = strlen(get_key);
		//printf("* %s %zu\n", get_key, keylen);
		get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		while(get_val == NULL)
		{
			get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
		}
		if(get_val != NULL)
		{
			//printf("%d %d\n", tmp , j);
			snprintf(exp_value, SWARMKV_MAX_VAL_LEN, "value-%d", j*10);
			EXPECT_EQ(*get_val, *exp_value); 
			printf("find: %s %s\n", get_val,exp_value);
			free(get_val);
			get_val = NULL;
		}
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 	
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(get_val);
	get_val = NULL;
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;

	sleep(60);
}
*/


/*
//串行访问
TEST_F(SwarmkvClusterTest, PUTafterMigratingTest)
{
	printf("******test Start Result******\n");
	while(1)
	{
		if(store[0]->meet_flag == 0 && store[1]->meet_flag == 0 && store[2]->meet_flag == 0 && store[3]->meet_flag == 0 &&
			store[4]->meet_flag == 0 && store[5]->meet_flag == 0 && store[6]->meet_flag == 0 && store[7]->meet_flag == 0 &&
			store[8]->meet_flag == 0 && store[9]->meet_flag == 0 && store[10]->meet_flag == 0 && store[11]->meet_flag == 0 &&
			store[12]->meet_flag == 0 && store[13]->meet_flag == 0 && store[14]->meet_flag == 0 && store[15]->meet_flag == 0
			)
		{
			break;
		}
	}
	sleep(6);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}

	sleep(10);

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<1000000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		vallen = strlen(value);
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("\nput_use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	
	sleep(30);
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	int cnt = 0;
	for(int j=0; j<1000000; j++)
	{
		//int tmp = rand()%100000;
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", j);
		size_t keylen = strlen(get_key);
		get_val = swarmkv_get(store[0], tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		while(get_val == NULL)
		{
			get_val = swarmkv_get_cache(store[0], tb_name, get_key, keylen, &get_vallen);
		}
		if(get_val != NULL)
		{
			if (strlen(get_val) == 0)
			{
				cnt++;
			}
			snprintf(exp_value, SWARMKV_MAX_VAL_LEN, "value-%d", j);
			EXPECT_EQ(*get_val, *exp_value); 
			free(get_val);
			get_val = NULL;
		}
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 	
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(get_val);
	get_val = NULL;
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
	printf("%d\n", cnt);

	sleep(60);
}
*/

/*
//测试update invalidate生效时间
TEST(OneNode, InvalidateSpeedTest)
{
	const char *bootstraps = "self=192.168.40.182:8323";
	const char *config = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err = NULL;
	struct swarmkv_store *store = swarmkv_open(bootstraps, config, &err);

	sleep(10);

	const char* tb_name = "table0";
	const char *key = "key0";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	
	//put
	char *put_callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	swarmkv_put_with_callback(store, tb_name, NULL, key, keylen, value, vallen, callback_with_arg, put_callback_value);

	sleep(5);

	//update
	const char *new_value = "valnew";
	size_t new_vallen = strlen(new_value);
	swarmkv_put_with_callback(store, tb_name, NULL, key, keylen, new_value, new_vallen, callback_with_arg, put_callback_value);
	struct timespec update_time = {0, 0};
	clock_gettime(CLOCK_REALTIME, &update_time);
	printf("update_time : %zu\n", update_time.tv_sec*1000 + update_time.tv_nsec/1000000);
}
*/

int main(int argc, char **argv) {
	printf("Running main() from %s\n", __FILE__);
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();   
}


/*
protected:
	//静态SetUpTestCase()和TearDownTestCase()是TestCase事件，在所有测试中只执行一次，所有的testcase开始前执行SetUpTestCase,结束后执行一次TearDownTestCase
	static void SetUpTestCase()
	{
		const char *bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store = swarmkv_open(bootstraps, config, &err);
	}
	static void TearDownTestCase()
	{
		swarmkv_close(store);
	}
*/



/*
TEST(SwarmkvClusterOpenTest, ThreeNode)
{
	const char *bootstraps_0 = "self=172.16.225.2:8323";
	const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_0 = NULL;
	struct swarmkv_store *store_0 = NULL;

	const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:8323";
	const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fc,db=0";
	char *err_1 = NULL;
	struct swarmkv_store *store_1 = NULL;

	const char *bootstraps_2 = "self=172.16.225.2:6323;peers=172.16.225.2:8323";
	const char *config_2 = "node_id=2;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fb,db=0";
	char *err_2 = NULL;
	struct swarmkv_store *store_2 = NULL;

	const char *bootstraps_3 = "self=172.16.225.2:7323;peers=172.16.225.2:8323";
	const char *config_3 = "node_id=3;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fa,db=0";
	char *err_3 = NULL;
	struct swarmkv_store *store_3 = NULL;

	store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);
	store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);
	store_2 = swarmkv_open(bootstraps_2, config_2, &err_2);
	store_3 = swarmkv_open(bootstraps_3, config_3, &err_3);


	sleep(60);

	swarmkv_GetSlotAssign(store_0);
	swarmkv_GetSlotAssign(store_1);
	swarmkv_GetSlotAssign(store_2);
	swarmkv_GetSlotAssign(store_3);

	free_store_space(store_0);
	free_store_space(store_1);
	free_store_space(store_2);
	free_store_space(store_3);
}
*/



