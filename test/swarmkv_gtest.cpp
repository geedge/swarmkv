#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include <gtest/gtest.h>
#include "swarmkv.h"

#include "../src/inc_internal/swarmkv_internal.h"
#include "../src/inc_internal/swarmkv_conhash.h"
/*
#include "../src/inc_internal/uthash/uthash.h"
#include "../src/inc_internal/uthash/utarray.h"
#include "../src/inc_internal/swarmkv_internal.h"
*/

//判断key在哪里，用于测试migrate成功与否
//swarmkv_getkeyopt(kv, key, SWMKV_OPT_KEY_LOCATION, *value);
//EXPECT_EQ (value, LOCAL_KEY);

/*
void get_callback_print(const char *table, const char *key, size_t keylen, const char *value, size_t vallen, void *arg)
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

class SwarmkvLocalTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	virtual void SetUp()
	{
		const char *bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
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
	const char* tb_name = "table0";
	size_t keylen = 0;
	size_t vallen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int i=0; i<100000; i++)
	{
		snprintf(key, SWARMKV_MAX_KEY_LEN, "key-%d", i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "value-%d", i);
		printf("* %s %s\n", key, value);
		vallen = strlen(value);
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
		printf("& %s\n", key);
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
	}
	printf("&&&\n");
	free(key);
	key = NULL;
	free(value);
	value = NULL;
	printf("***\n");

	size_t get_vallen = 0;
	char *get_key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *get_val = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *exp_value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	for(int j=0; j<100000; j++)
	{
		snprintf(get_key, SWARMKV_MAX_KEY_LEN, "key-%d", j);
		keylen = strlen(get_key);
		get_val = swarmkv_get(store_0, tb_name, NULL, get_key, keylen, &get_vallen, NULL);
		snprintf(exp_value, SWARMKV_MAX_KEY_LEN, "value-%d", j);
		EXPECT_EQ(*get_val, *exp_value); 
		memset(get_key, 0, SWARMKV_MAX_KEY_LEN);
		memset(get_val, 0, SWARMKV_MAX_VAL_LEN); 
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
//测试本地put一个kv并get_with_callback
TEST_F(SwarmkvLocalTest, PutGetCallbLocal)
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
}

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
	swarmkv_get_with_callback(store_0, tb_name, NULL, key, keylen, get_callback_print, NULL);
	
	sleep(1);
	//check from store_0 cache
	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_cache_get_kv(store_0, tb_name, key, keylen, &get_vallen);
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);   
	swarmkv_close(store_1);
}

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
	swarmkv_get_with_callback(store_1, tb_name, NULL, key, keylen, get_callback_print, NULL);
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
	swarmkv_put_with_callback(store_0, tb_name, NULL, key, keylen, value, vallen, get_callback_print, NULL);
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

class SwarmkvClusterTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	struct swarmkv_store *store_1 = NULL;
	struct swarmkv_store *store_2 = NULL;
	struct swarmkv_store *store_3 = NULL;
	virtual void SetUp()
	{
		const char *bootstraps_0 = "self=172.16.225.2:8323";
		const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err_0 = NULL;

		const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:8323";
		const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fc,db=0";
		char *err_1 = NULL;

		const char *bootstraps_2 = "self=172.16.225.2:6323;peers=172.16.225.2:8323";
		const char *config_2 = "node_id=2;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fb,db=0";
		char *err_2 = NULL;

		const char *bootstraps_3 = "self=172.16.225.2:7323;peers=172.16.225.2:8323";
		const char *config_3 = "node_id=3;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fa,db=0";
		char *err_3 = NULL;

		store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);
		store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);
		store_2 = swarmkv_open(bootstraps_2, config_2, &err_2);
		store_3 = swarmkv_open(bootstraps_3, config_3, &err_3);

		sleep(10);
	}

	virtual void TearDown()
	{
		printf("begine down\n");
		free_store_space(store_0);
		free_store_space(store_1);
		free_store_space(store_2);
		free_store_space(store_3);
		printf("end\n");
	}
};


TEST_F(SwarmkvClusterTest, StartResultTest)
{

	printf("******test Start Result******\n");
	swarmkv_GetSlotAssign(store_0);
	swarmkv_GetSlotAssign(store_1);
	swarmkv_GetSlotAssign(store_2);
	swarmkv_GetSlotAssign(store_3);

	int NID[4] = {0,1,2,3};
	check_slot_table_state(store_0, NID, 4);
	check_slot_table_state(store_1, NID, 4);
	check_slot_table_state(store_2, NID, 4);
	check_slot_table_state(store_3, NID, 4);
}

int main(int argc, char **argv) {
  printf("Running main() from %s\n", __FILE__);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();   
}


/*
GTEST_API_ int main(int argc, char **argv) {
    std::cout << "Running main() from gtest_main.cc\n";
	//解析命令行中的GoogleTest参数，它允许用户通过多样的命令行参数来控制测试程序的行为（即定制命令行参数的行为）
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
*/

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