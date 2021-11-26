#include<stdio.h>
#include<pthread.h>
#include <gtest/gtest.h>
#include "swarmkv.h"

void get_callback_print(const char *table, const char *key, size_t keylen, const char *value, size_t vallen, void *arg)
{
	printf("\n-----print callback result-----\n");
	printf(" table: %s\n", table);
	printf("   key: %s\n", key);
	printf("keylen: %ld\n", keylen);
	printf(" value: %s\n", value);
	printf("vallen: %ld\n", vallen);
	
	EXPECT_STREQ(value,"val0"); 
	printf("\n");
}

class SwarmkvLocalTest : public testing::Test 
{
protected:
	struct swarmkv_store *store = NULL;
	virtual void SetUp()
	{
		const char *bootstraps = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store = swarmkv_open(bootstraps, config, &err);
	}
	virtual void TearDown()
	{
		swarmkv_close(store);
	}
};

//测试本地put一个kv并get
TEST_F(SwarmkvLocalTest, PutGetLocal)
{
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);   
	free(get_val);
	get_val = NULL;
}

//测试本地put一个kv并get_with_callback
TEST_F(SwarmkvLocalTest, PutGetCallbLocal)
{
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);   
	free(get_val);
	get_val = NULL;
}


TEST_F(SwarmkvLocalTest, UpdateLocal)
{
	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);
	swarmkv_put(store, tb_name, NULL, key, keylen, value, vallen, NULL);

	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	
	const char *exp0 = "val0";
	EXPECT_EQ(*get_val, *exp0);  
	free(get_val);
	get_val = NULL;

	const char *new_value = "val1";
	size_t new_vallen = strlen(new_value);
	swarmkv_put(store, tb_name, NULL, key, keylen, new_value, new_vallen, NULL);
	get_val = swarmkv_get(store, tb_name, NULL, key, keylen, &get_vallen, NULL);
	
	const char *exp1 = "val1";
	EXPECT_EQ(*get_val, *exp1);
	free(get_val);
	get_val = NULL;
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
		swarmkv_close(store_0);
		swarmkv_close(store_1);
	}
};


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