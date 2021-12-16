#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include <gtest/gtest.h>
#include "swarmkv.h"
#include<time.h>

//#include "../src/inc_internal/swarmkv_internal.h"
//#include "../src/inc_internal/swarmkv_conhash.h"
#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"
/*
#include "../src/inc_internal/uthash/uthash.h"
#include "../src/inc_internal/uthash/utarray.h"
#include "../src/inc_internal/swarmkv_internal.h"
*/

//判断key在哪里，用于测试migrate成功与否
//swarmkv_getkeyopt(kv, key, SWMKV_OPT_KEY_LOCATION, *value);
//EXPECT_EQ (value, LOCAL_KEY);

const char* host_ip = "192.168.40.182";

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
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->slots[slot_id].table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I don't have the table in my cache!\n");
	}
	else
	{
		HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
		if(kv == NULL)
		{
			//printf("I don't find the key in my cache!\n");
		}
		else
		{
			value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
			memcpy(value, kv->value, kv->vallen);
			*vallen = kv->vallen;
		}
	}
	pthread_rwlock_unlock(&store->rwlock);
	return value;
}
/*————————————————————————————————————测试用例————————————————————————————————————————*/
/*————————————————————————————————————测试用例————————————————————————————————————————*/
/*————————————————————————————————————测试用例————————————————————————————————————————*/

/*
class SwarmkvLocalTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	virtual void SetUp()
	{
		char *bootstraps = ALLOC(char, 128); 
		snprintf(bootstraps, SWARMKV_MAX_VAL_LEN, "self=%s:8323", host_ip);
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store_0 = swarmkv_open(bootstraps, config, &err);
	}
	virtual void TearDown()
	{
		free_store_space(store_0);
	}
};

//测试本地put一个kv并get
TEST_F(SwarmkvLocalTest, PutGetLocal)
{
	printf("******test PUT_Local and GET_LOCAL speed******\n");
	const char* tb_name = "table0";
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);

	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<1000000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		//printf("* %s %s\n", key, value);
		vallen = strlen(value);
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);

	//struct timespec start_time = {0, 0};
	//struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	size_t get_vallen = 0;
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int j=0; j<1000000; j++)
	{
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", j);
		keylen = strlen(get_key);
		get_val = swarmkv_get(store_0, tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", j);
		EXPECT_EQ(*get_val, *exp_value); 
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(exp_value, 0, SWARMKV_MAX_VAL_LEN); 
		free(get_val);
		get_val = NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
	free(get_key);
	get_key = NULL;
	free(exp_value);
	exp_value = NULL;
}

//测试本地put一个kv并get_with_callback，不带参数的callback函数
TEST_F(SwarmkvLocalTest, GetCallbackLocal)
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
TEST_F(SwarmkvLocalTest, GetCallbackwithArgLocal)
{
	printf("******test GET_LOCAL with callback(Arg)******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	swarmkv_get_with_callback(store_0, tb_name, NULL, key, keylen, callback_with_arg, callback_value);

	printf("%s\n", callback_value);
	const char *exp0 = "val0";
	EXPECT_EQ(*callback_value, *exp0); 

	free(callback_value);
	callback_value = NULL;
}

//测试本地put_with_callback
TEST_F(SwarmkvLocalTest, PUTCallbackLocal)
{
	printf("******test PUT_LOCAL with callback******\n");
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put_with_callback(store_0, tb_name, NULL, key, keylen, value, vallen, callback_print, NULL);

}

//测试本地put_with_callback
TEST_F(SwarmkvLocalTest, PUTCallbackwithArgLocal)
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
*/


/*
class SwarmkvRemoteTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[3];
	virtual void SetUp()
	{
		const char *bootstraps = "self=192.168.40.182:1323";
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store[0] = swarmkv_open(bootstraps, config, &err);
		for(int i=1; i<3; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			snprintf(bootstraps, SWARMKV_MAX_VAL_LEN, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			snprintf(config, SWARMKV_MAX_VAL_LEN, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
		}
	}
	virtual void TearDown()
	{
		//free_store_space(store[0]);
	}
};



//测试远程put_with_callback
TEST_F(SwarmkvRemoteTest, PUTCallbackwithArgLocal)
{
	printf("******test PUT_Remote with callback(Arg)******\n");
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

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	char *callback_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	int slot_id = caculate_key_slot_id(key, keylen);
	printf("slot_id: %d\n", slot_id);

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
*/

/*
//测试本地缓存和invalidate
TEST_F(SwarmkvRemoteTest, CacheandInvalidate)
{
	printf("******test get remote & cache accuracy and invalidate speed******\n");
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
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000000 + (end_time.tv_nsec - start_time.tv_nsec)/1000);
	printf("get value: %s\n", get_callback_value);

	//check cache
	char *cache_value = NULL;
	size_t get_vallen;
	cache_value = swarmkv_get_cache(store[0], tb_name, key, keylen, &get_vallen);
	printf("cache_value: %s\n", cache_value);
	EXPECT_EQ(*cache_value, *exp_val); 
	free(cache_value);
	cache_value = NULL;

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
*/


/*
//在本地节点上，测试对于同一个key，更新value
TEST_F(SwarmkvLocalTest, UpdateLocal)
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

//测试meet&migrate
TEST_F(SwarmkvLocalTest, MeetandMigrate)
{
	printf("******test GET_REMOTE******\n");

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	struct swarmkv_store *store_1 = NULL;
	const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
	const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_1 = NULL;
	store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);

	sleep(1);
	swarmkv_get_with_callback(store_0, tb_name, NULL, key, keylen, callback_print, NULL);
	
	sleep(1);
	//check from store_0 cache
	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_cache_get_kv(store_0, tb_name, key, keylen, &get_vallen);
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);   
	swarmkv_close(store_1);
}
*/

/*
class SwarmkvClusterCommuTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	struct swarmkv_store *store_1 = NULL;
	virtual void SetUp()
	{
		const char *bootstraps_0 = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
		const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err_0 = NULL;
		store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);

		const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
		const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err_1 = NULL;
		store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);
	}
	virtual void TearDown()
	{
		printf("begine down\n");
		swarmkv_close(store_0);
		swarmkv_close(store_1);
		printf("end\n");
	}
};

//测试put remote
TEST_F(SwarmkvClusterCommuTest, PutRemote)
{
	printf("******test Put Remote******\n");

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	sleep(1);
	swarmkv_get_with_callback(store_1, tb_name, NULL, key, keylen, callback_print, NULL);
}

//测试put remote with callback
TEST_F(SwarmkvClusterCommuTest, PutRemotewithCallback)
{
	printf("******test Put Remote with callback******\n");

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put_with_callback(store_0, tb_name, NULL, key, keylen, value, vallen, callback_print, NULL);
	sleep(1);
}
*/

/*
//测试update remote and invaliadate
TEST_F(SwarmkvClusterCommuTest, PutRemote)
{
	printf("******test Put Remote with callback******\n");

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);

	sleep(1);
	const char *value = "val0_new";
	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);	
}
*/


/*
class SwarmkvClusterInitTest : public testing::Test 
{
protected:
	struct swarmkv_store *store[16];
	virtual void SetUp()
	{
		const char *bootstraps = "self=192.168.40.182:1323";
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store[0] = swarmkv_open(bootstraps, config, &err);
		for(int i=1; i<16; i++)
		{
			char *bootstraps = ALLOC(char, 128); 
			char *config = ALLOC(char, 128);
			char *err = NULL;
			snprintf(bootstraps, SWARMKV_MAX_VAL_LEN, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			snprintf(config, SWARMKV_MAX_VAL_LEN, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
		}
	}
	virtual void TearDown()
	{		
		printf("test end\n");
	}
};
*/

/*
//测试初始化16个节点的时间
TEST_F(SwarmkvClusterInitTest, EmptyStartSpeed)
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
	sleep(15);
	int NID[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
	for(int i=0; i<16; i++)
	{
		check_slot_table_state(store[i], NID, 16);
	}
}
*/


/*
//测试在十六个节点集群中随机读取的时间
TEST_F(SwarmkvClusterInitTest, RadomGetSpeed)
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

	//put
	printf("start puts\n");
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<1000000; i++)
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

	sleep(60);

	//get
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = NULL;
	size_t get_vallen = 0;
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	printf("start time :\n");
	for(int j=0; j<10000; j++)
	{
		int tmp = rand()%1000000;
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
			snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", tmp);
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

	sleep(10);

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
			snprintf(bootstraps, SWARMKV_MAX_VAL_LEN, "self=%s:%d323;peers=%s:1323", host_ip, i+1, host_ip);
			snprintf(config, SWARMKV_MAX_VAL_LEN, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
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
		int tmp = rand()%1000000;
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
				snprintf(bootstraps, 128, "self=172.16.225.2:%d323", i+1);
			}
			else
			{
				snprintf(bootstraps, 128, "self=172.16.225.2:%d323;peers=172.16.225.2:1323", i+1);
			}
			snprintf(config, 128, "node_id=%d;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0", i);
			store[i] = swarmkv_open(bootstraps, config, &err);
			free(bootstraps);
			bootstraps = NULL;
			free(config);
			config = NULL;
		}
		//sleep(20);
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

	const char* tb_name = "table0";
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<1000000; i++)
	{
		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		size_t keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		printf("* %s %s\n", key, value);
		vallen = strlen(value);
		swarmkv_put(store[0], tb_name, NULL, key, keylen, value, vallen, NULL);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	free(key);
	key = NULL;
	free(value);
	value = NULL;
}


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