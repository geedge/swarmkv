#include "swarmkv.h"
#include <gtest/gtest.h>
class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		swarmkv_open(const char * bootstraps, const char * config, char * * err);
	}
	static void TearDownTestCase()
	{
		swarmkv_close(store);
	}	
 	// Some expensive resource shared by all tests.
	static struct swarmkv_store* store;
};
struct swarmkv_store* SwarmkvBasicTest::store;

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
