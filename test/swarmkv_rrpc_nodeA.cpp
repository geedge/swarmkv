#include <cstdio>
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include "swarmkv.h"
#include <ctime>
#include <MESA/field_stat2.h>

#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"

const char *host_ip = "10.1.1.1";

static void get_random_str(char *random_str, const int random_len) {
    size_t seed_str_len;
    struct timeval tv;
    unsigned int seed_num;
    char seed_str[] = "abcdefghijklmnopqrstuvwxyz"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; //随机字符串的随机字符集

    seed_str_len = strlen(seed_str);

    gettimeofday(&tv, NULL);
    seed_num = (unsigned int) (tv.tv_sec + tv.tv_usec); //超了unsigned int的范围也无所谓，我们要的只是不同的种子数字
    srand(seed_num);

    for (int i = 0; i < random_len; i++) {
        random_str[i] = seed_str[rand() % seed_str_len];
    }
}

class SwarmkvLocalTest : public testing::Test {
 protected:
  struct swarmkv_store *store_0 = NULL;
  virtual void SetUp() {
      char *bootstraps = ALLOC(char, 128);
      snprintf(bootstraps, 128, "proto=udp;self=%s:8323", host_ip);
      const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
      char *err = NULL;
      store_0 = swarmkv_open(bootstraps, config, &err);
  }
  virtual void TearDown() {
      pthread_cancel(store_0->tid);
      pthread_join(store_0->tid, NULL);
      free_store_space(store_0);
  }
};

TEST_F(SwarmkvLocalTest, SimplePut) {
    sleep(20);

    printf("******test PUT******\n");
    const char *tb_name = "table0";
    printf("%s\n", tb_name);
    char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
    char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
    char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
    char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);

    int kv_len = 64;

    get_random_str(key_prefix, kv_len);
    get_random_str(val_prefix, kv_len);

    struct timespec start_time = {0, 0};
    struct timespec end_time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (int i = 0; i < 1000000; i++) {

        snprintf(key, SWARMKV_MAX_KEY_LEN, "%d-%s", i, key_prefix);
        snprintf(value, SWARMKV_MAX_VAL_LEN, "%d-%s", i, val_prefix);

        swarmkv_put(store_0, tb_name, NULL, key, strlen(key), value, strlen(value), NULL);

        memset(key, 0, SWARMKV_MAX_KEY_LEN);
        memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
        memset(value, 0, SWARMKV_MAX_VAL_LEN);
        memset(val_prefix, 0, SWARMKV_MAX_VAL_LEN);
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("use_time : %zu\n",
           (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000);
    free(key);
    key = NULL;
    free(value);
    value = NULL;
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


