#include "swarmkv/swarmkv.h"
#include <gtest/gtest.h>
#include <MESA/MESA_handle_logger.h>

class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		void* logger=NULL;
		const char* log_path="./basic-test.log";
		logger=MESA_create_runtime_log_handle(log_path, 0);
		store=swarmkv_open("swarmkv-basic-test", 5210, 5211, logger);
		if(!store)
		{
			printf("swarmkv_open failed, see %s for details.\n", log_path);
		}
	}
	static void TearDownTestCase()
	{
		swarmkv_close(store);
	}	
 	// Some expensive resource shared by all tests.
	static struct swarmkv_store* store;
};
struct swarmkv_store* SwarmkvBasicTest::store;
TEST_F(SwarmkvBasicTest, SimpleGet)
{
	struct swarmkv_store* store=SwarmkvBasicTest::store;
	const char* key="key1";
	swarmkv_get_string(store, key, strlen(key), NULL, NULL);
	sleep(1800);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
