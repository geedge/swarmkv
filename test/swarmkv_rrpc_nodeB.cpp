#include <cstdio>
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <ctime>
#include "swarmkv.h"
#include<MESA/field_stat2.h>

#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"

const char *host_ip = "10.1.2.1";

class SwarmkvLocalTest : public testing::Test {
 protected:
  struct swarmkv_store *store_0 = NULL;
  virtual void SetUp() {
      char *bootstraps = ALLOC(char, 128);
      snprintf(bootstraps, 128, "proto=udp;self=%s:8323;peers=10.1.1.1:8323", host_ip);
      const char *config = "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
      char *err = NULL;
      store_0 = swarmkv_open(bootstraps, config, &err);
  }
  virtual void TearDown() {
      pthread_cancel(store_0->tid);
      pthread_join(store_0->tid, NULL);
      free_store_space(store_0);
  }
};

//测试本地put kv并get key:5～10bytes value:7~12bytes
TEST_F(SwarmkvLocalTest, SimplePut) {
    sleep(200);
}

int main(int argc, char **argv) {
    printf("Running main() from %s\n", __FILE__);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}





