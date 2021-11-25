#include<stdio.h>
#include<pthread.h>
#include <gtest/gtest.h>
#include "swarmkv.h"

TEST(testSwarmkvLocalPutGet, simpletest)
{
    const char *bootstraps_0 = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
	const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_0 = NULL;
	struct swarmkv_store *store_0 = NULL;
	store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);

	const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
	const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_1 = NULL;
	struct swarmkv_store *store_1 = NULL;
	store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);

	const char* tb_name = "table0";
	const char *key = "key3";
	size_t keylen = strlen(key);
	const char *value = "val0";
	size_t vallen = strlen(value);

	swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);

	sleep(1);
	char *get_val = NULL;
	size_t get_vallen;
	get_val = swarmkv_get(store_0, tb_name, NULL, key, keylen, &get_vallen, NULL);
	printf("get_val: %s\n",get_val);
	int ret = strcmp(get_val,"val0");  
	EXPECT_EQ(ret,0);  
	free(get_val);
	get_val = NULL;

	swarmkv_close(store_0);
	swarmkv_close(store_1);
}

TEST(testSwarmkvMigrate, simpletest)
{
	const char *bootstraps_0 = "self=172.16.225.2:8323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:18323";
	const char *config_0 = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_0 = NULL;
	struct swarmkv_store *store_0 = NULL;
	store_0 = swarmkv_open(bootstraps_0, config_0, &err_0);

	const char *bootstraps_1 = "self=172.16.225.2:18323;peers=172.16.225.2:6323,172.16.225.2:7323,172.16.225.2:8323";
	const char *config_1 = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char *err_1 = NULL;
	struct swarmkv_store *store_1 = NULL;
	store_1 = swarmkv_open(bootstraps_1, config_1, &err_1);
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
