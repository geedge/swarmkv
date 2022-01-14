#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<string.h>
#include <gtest/gtest.h>
#include "swarmkv.h"
#include<time.h>
#include<MESA/field_stat2.h>

#include "swarmkv_internal.h"
#include "swarmkv_conhash.h"


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

int get_random_str(char* random_str, const int random_len)
{
    int i, random_num, seed_str_len;
    struct timeval tv;
    unsigned int seed_num;
    char seed_str[] = "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; //随机字符串的随机字符集

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

class SwarmkvLocalTest : public testing::Test 
{
protected:
	struct swarmkv_store *store_0 = NULL;
	virtual void SetUp()
	{
		char *bootstraps = ALLOC(char, 128); 
		snprintf(bootstraps, 128, "proto=udp;self=%s:8323", host_ip);
		const char *config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
		char *err = NULL;
		store_0 = swarmkv_open(bootstraps, config, &err);
	}
	virtual void TearDown()
	{
		free_store_space(store_0);
	}
};

//测试本地put kv并get key:5～10bytes value:7~12bytes
TEST_F(SwarmkvLocalTest, SimplePut)
{
	sleep(10);

	printf("******test PUT******\n");
	const char* tb_name = "table0";
	printf("%s\n", tb_name);
	size_t keylen = 0;
	char *key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	char *key_prefix = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	char *val_prefix = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	//size_t get_vallen = 0;
	int kv_prefix_len = 16;
    int val_prefix_len = 1000;
	
	struct timespec start_time = {0, 0};
	struct timespec end_time = {0, 0};
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for(int i=0; i<10000; i++)
	{
		get_random_str(key_prefix, kv_prefix_len);
		get_random_str(val_prefix, val_prefix_len);

		size_t vallen = 0;
		snprintf(key, SWARMKV_MAX_KEY_LEN, "%s-%d", key_prefix, i);
		keylen = strlen(key);
		snprintf(value, SWARMKV_MAX_VAL_LEN, "%s-%d", val_prefix, i);
		vallen = strlen(value);		
		
		swarmkv_put(store_0, tb_name, NULL, key, keylen, value, vallen, NULL);
	
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(key_prefix, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		memset(val_prefix, 0, SWARMKV_MAX_VAL_LEN);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("use_time : %zu\n", (end_time.tv_sec - start_time.tv_sec)*1000 + (end_time.tv_nsec - start_time.tv_nsec)/1000000);
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


