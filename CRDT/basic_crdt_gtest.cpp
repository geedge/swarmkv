#include "lww_register.h"
#include "pn_counter.h"
#include "or_set.h"
#include "or_map.h"
#include "g_array.h"
#include "crdt_utils.h"

#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>
#include <math.h>

TEST(LWWRegister, Basic)
{
	struct LWW_register *reg=NULL;
	uuid_t uuid;
	uuid_generate(uuid);
	reg=LWW_register_new(uuid);
	
	const char *value0=NULL;
	size_t val_sz=0;
	LWW_register_get0(reg, &value0, &val_sz);
	EXPECT_TRUE(value0==NULL);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "abc123");
	LWW_register_set(reg, tmp, strlen(tmp)+1);
	LWW_register_get0(reg, &value0, &val_sz);
	EXPECT_STREQ(tmp, value0);
	value0=NULL;

	char *value1=NULL;
	snprintf(tmp, sizeof(tmp), "abcdefg");
	LWW_register_set(reg, tmp, strlen(tmp)+1);
	LWW_register_get1(reg, &value1, &val_sz);
	EXPECT_STREQ(tmp, value1);
	free(value1);

	LWW_register_free(reg);
		
	return;
}
TEST(LWWRegister, Merge)
{
	struct LWW_register *reg[2];	
	uuid_t uuid;

	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		reg[i]=LWW_register_new(uuid);
	}
	const char *value0=NULL;
	size_t val_sz=0;
	LWW_register_get0(reg[0], &value0, &val_sz);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "abc123");
	LWW_register_set(reg[0], tmp, strlen(tmp)+1);
	EXPECT_TRUE(value0==NULL);

	char *blob=NULL;
	size_t blob_sz=0;
	LWW_register_serialize(reg[0], &blob, &blob_sz);
	LWW_register_merge_blob(reg[1], blob, blob_sz);
	free(blob);
	
	char *value1=NULL;	
	LWW_register_get1(reg[1], &value1, &val_sz);
	EXPECT_STREQ(tmp, value1);
	free(value1);

	for(size_t i=0; i<2; i++)
	{
		LWW_register_free(reg[i]);
	}	
	return;
}
TEST(LWWRegister, Sequential)
{
	struct LWW_register *reg[2];
	uuid_t uuid;
	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		reg[i]=LWW_register_new(uuid);
	}
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "reg1:abc123");
	LWW_register_set(reg[0], tmp, strlen(tmp)+1);
	usleep(10);
	
	snprintf(tmp, sizeof(tmp), "reg2:abcdefg");
	LWW_register_set(reg[1], tmp, strlen(tmp)+1);


	char *blob=NULL;
	size_t blob_sz=0;
	LWW_register_serialize(reg[0], &blob, &blob_sz);
	LWW_register_merge_blob(reg[1], blob, blob_sz);
	free(blob);

	LWW_register_serialize(reg[1], &blob, &blob_sz);
	LWW_register_merge_blob(reg[0], blob, blob_sz);
	free(blob);

	char *value=NULL;
	size_t val_sz=0;
	LWW_register_get1(reg[0], &value, &val_sz);
	EXPECT_STREQ(tmp, value);
	free(value);
	
	LWW_register_get1(reg[1], &value, &val_sz);
	EXPECT_STREQ(tmp, value);
	free(value);

	for(size_t i=0; i<2; i++)
	{
		LWW_register_free(reg[i]);
	}	
	return;
}
void PN_counter_sync(struct PN_counter *list[], size_t n)
{
	char *blob=NULL;
	size_t blob_sz=0;
	
	for(size_t i=0; i<n; i++)
	{
		PN_counter_serialize(list[i], &blob, &blob_sz);
		for(size_t j=0; j<n; j++)
		{
			PN_counter_merge_blob(list[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	return;
}
TEST(PNCounter, Basic)
{
	struct PN_counter *pnc=NULL;
	uuid_t uuid;
	uuid_generate(uuid);
	long long value=0;
	pnc=PN_counter_new(uuid);
	value=PN_counter_get(pnc);
	EXPECT_EQ(value, 0);

	value=PN_counter_incrby(pnc, 100);
	EXPECT_EQ(value, 100);

	value=PN_counter_incrby(pnc, -20);
	EXPECT_EQ(value, 80);

	value=PN_counter_set(pnc, 2379);
	EXPECT_EQ(value, 2379);

	PN_counter_free(pnc);
}
TEST(PNCounter, Merge)
{	
	struct PN_counter *pnc[2];
	uuid_t uuid;
	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		pnc[i]=PN_counter_new(uuid);
	}
	long long value;
	PN_counter_incrby(pnc[0], 200);
	PN_counter_incrby(pnc[1], -100);

	PN_counter_sync(pnc, 2);

	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 100);
	
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 100);

	PN_counter_set(pnc[0], 0);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 0);


	PN_counter_set(pnc[0], 1314);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 1314);


	PN_counter_set(pnc[0], 3344);
	PN_counter_incrby(pnc[1], -11);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 3333);

	for(size_t i=0; i<2; i++)
	{
		PN_counter_free(pnc[i]);
	}

}
TEST(PNCounter, ObservedSet)
{
	struct PN_counter *pnc[2];
	uuid_t uuid;
	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		pnc[i]=PN_counter_new(uuid);
	}
	long long value=0;
	PN_counter_incrby(pnc[0], 2);
	PN_counter_set(pnc[0], 3);
	PN_counter_incrby(pnc[1], -1);
	
	PN_counter_sync(pnc, 2);
	
	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 2);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 2);

	PN_counter_incrby(pnc[0], 7);	
	PN_counter_sync(pnc, 2);
	PN_counter_set(pnc[1], 8);
	PN_counter_sync(pnc, 2);

	
	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 8);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 8);

	PN_counter_set(pnc[0], 10);
	usleep(10);
	PN_counter_set(pnc[1], 11);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 11);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 11);

	PN_counter_incrby(pnc[0], 1);	
	PN_counter_sync(pnc, 2);
	usleep(10);
	PN_counter_set(pnc[1], 13);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 13);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 13);

	PN_counter_set(pnc[0], 15);
	usleep(10);
	PN_counter_incrby(pnc[1], -10);
	PN_counter_sync(pnc, 2);
	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 5);
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 5);

	for(size_t i=0; i<2; i++)
	{
		PN_counter_free(pnc[i]);
	}
}

TEST(PNCounter, Replica100)
{
	size_t test_replica_num=100, i=0;	
	uuid_t uuid;

	struct PN_counter *pnc[test_replica_num];
	for(i=0; i<test_replica_num; i++)
	{
		uuid_generate(uuid);
		pnc[i]=PN_counter_new(uuid);
	}
	long long expect_value=0, actual_value=0, increment=0;
	srand(171);
	size_t node_idx=0, loop=100000;
	for(i=0; i<loop; i++)
	{
		node_idx=rand()%test_replica_num;
		increment=2*rand();//to generate negtive expect_value
		PN_counter_incrby(pnc[node_idx], increment);
		expect_value+=increment;
		
	}
	PN_counter_sync(pnc, test_replica_num);

	size_t success=0;
	for(i=0; i<test_replica_num; i++)
	{
		actual_value=PN_counter_get(pnc[i]);
		if(expect_value==actual_value)
		{
			success++;
		}
	}
	EXPECT_EQ(success, test_replica_num);
	size_t sz=0;
	sz=PN_counter_mem_size(pnc[0]);
	EXPECT_EQ(sz, 8864);
	for(i=0; i<test_replica_num; i++)
	{
		PN_counter_free(pnc[i]);
	}	
}
TEST(ORSet, Basic)
{
	int ret=0;
	uuid_t uuid;
	uuid_generate(uuid);

	const char *members[]={"zhangsan", "lisi", "Jack"};
	struct OR_set *set=OR_set_new(uuid);
	for(size_t i=0; i<3; i++)
	{
		ret=OR_set_add(set, members[i], strlen(members[i]));
		EXPECT_EQ(ret, 1);
	}
	ret=OR_set_is_member(set, members[0], strlen(members[0]));
	EXPECT_EQ(ret, 1);

	ret=OR_set_remove(set, members[0], strlen(members[0]));
	EXPECT_EQ(ret, 1);
	ret=OR_set_is_member(set, members[0], strlen(members[0]));
	EXPECT_EQ(ret, 0);

	ret=OR_set_is_member(set, members[1], strlen(members[1]));
	EXPECT_EQ(ret, 1);

	size_t n_member=0;
	n_member=OR_set_cardinality(set);
	EXPECT_EQ(n_member, 2);
	struct OR_set_member_list *list=NULL;
	list=OR_set_members(set);
	EXPECT_EQ(list->n_member, 2);
	OR_set_member_list_free(list);
	OR_set_free(set);	
}
TEST(ORSet,   Merge01)
{
	struct OR_set *set[2];
	uuid_t uuid;
	size_t i=0;
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new(uuid);
	}
	char buffer[256];
	size_t n_test_member=100;
	for(i=0; i<n_test_member; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		OR_set_add(set[0], buffer, strlen(buffer)+1);
	}
	char *blob=NULL;
	size_t blob_sz=0;
	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge_blob(set[1], blob, blob_sz);
	size_t success_cnt=0;
	for(i=0; i<n_test_member; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		success_cnt+=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	}
	EXPECT_EQ(success_cnt, n_test_member);

	for(i=0; i<2; i++)
	{
		OR_set_free(set[i]);
		set[i]=NULL;
	}
	free(blob);
}
TEST(ORSet, Merge11)
{
	struct OR_set *set[2];
	uuid_t uuid;
	size_t i=0;

	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new(uuid);
	}
	size_t n_test_member=100;
	for(i=0; i<n_test_member; i++)
	{
		OR_set_add(set[i%2], (const char*)&i, sizeof(i));
	}

	char *blob=NULL;
	size_t blob_sz=0;

	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge_blob(set[1], blob, blob_sz);
	free(blob);

	OR_set_serialize(set[1], &blob, &blob_sz);
	OR_set_merge_blob(set[0], blob, blob_sz);
	free(blob);

	size_t success_cnt=0;
	for(i=0; i<n_test_member; i++)
	{
		success_cnt+=OR_set_is_member(set[0], (const char*)&i, sizeof(i));
		success_cnt+=OR_set_is_member(set[1], (const char*)&i, sizeof(i));
	}
	
	EXPECT_EQ(success_cnt, n_test_member*2);
	
	for(i=0; i<2; i++)
	{
		OR_set_free(set[i]);
		set[i]=NULL;
	}

}
TEST(ORSet,   Remove)
{
	struct OR_set *set[2];
	uuid_t uuid;
	size_t i=0;
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new(uuid);
	}
	char buffer[256];
	size_t n_test_member=100;
	for(i=0; i<n_test_member; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		OR_set_add(set[0], buffer, strlen(buffer)+1);
	}

	char *blob=NULL;
	size_t blob_sz=0;
	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge_blob(set[1], blob, blob_sz);
	free(blob);
	
	int is_member=0;
	snprintf(buffer, sizeof(buffer), "ORSet-Member-0");
	is_member=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 1);
	
	OR_set_remove(set[0], buffer, strlen(buffer)+1);
	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge_blob(set[1], blob, blob_sz);
	free(blob);

	is_member=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 0);

	for(i=0; i<2; i++)
	{
		OR_set_free(set[i]);
		set[i]=NULL;
	}
}

TEST(ORSet,   ObservedRemove)
{
	struct OR_set *set[2];
	uuid_t uuid;
	size_t i=0;
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new(uuid);
	}
	
	char buffer[256];
	size_t n_test_member=100;
	for(i=0; i<n_test_member; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		OR_set_add(set[0], buffer, strlen(buffer)+1);
		OR_set_add(set[1], buffer, strlen(buffer)+1);
	}
	int is_member=0;
	snprintf(buffer, sizeof(buffer), "ORSet-Member-0");
	OR_set_remove(set[0], buffer, strlen(buffer)+1);
	is_member=OR_set_is_member(set[0], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 0);

	char *blob=NULL;
	size_t blob_sz=0;
	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge_blob(set[1], blob, blob_sz);
	free(blob);

	is_member=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 1);

	
	OR_set_serialize(set[1], &blob, &blob_sz);
	OR_set_merge_blob(set[0], blob, blob_sz);
	free(blob);

	is_member=OR_set_is_member(set[0], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 1);
	
	for(i=0; i<2; i++)
	{
		OR_set_free(set[i]);
		set[i]=NULL;
	}	
}
TEST(ORSet, Replica32)
{
	size_t replica_num=32, member_num=4096;
	struct OR_set *set[replica_num];
	uuid_t uuid;
	size_t i=0, j=0;
	for(i=0; i<replica_num; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new(uuid);
	}
	char buffer[256];
	for(i=0; i<member_num; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		OR_set_add(set[i%replica_num], buffer, strlen(buffer)+1);
	}
	
	char *blob=NULL;
	size_t blob_sz=0;
	for(i=0; i<replica_num; i++)
	{
		OR_set_serialize(set[i], &blob, &blob_sz);
		for(j=0; j<replica_num; j++)
		{
			OR_set_merge_blob(set[j], blob, blob_sz);
		}
		free(blob);
		blob_sz=0;
	}
	size_t success_cnt=0;
	for(i=0; i<member_num; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		for(j=0; j<replica_num; j++)
		{
			success_cnt+=OR_set_is_member(set[j], buffer, strlen(buffer)+1);
		}
	}
	EXPECT_EQ(success_cnt, member_num*replica_num);

	size_t n_replica=0;
	success_cnt=0;
	for(i=0; i<replica_num; i++)
	{
		n_replica=OR_set_replica_number(set[i]);
		if(n_replica==replica_num)
		{
			success_cnt++;
		}
		OR_set_free(set[i]);
		set[i]=NULL;
	}
	EXPECT_EQ(success_cnt, replica_num);
}
void OR_map_sync(struct OR_map **maps, size_t n_map)
{
	char *blob=NULL;
	size_t blob_sz=0;
	for(size_t i=0; i<n_map; i++)
	{
		for(size_t j=0; j<n_map; j++)
		{
			if(i==j) continue;
			OR_map_serialize(maps[j], &blob, &blob_sz);
			OR_map_merge_blob(maps[i], blob, blob_sz);
			free(blob);
			blob=NULL;
		}
	}
}
TEST(ORMap, Basic)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct OR_map *map=OR_map_new(uuid);

	const char *key1="zhangsan";
	int ret=0;
	ret=OR_map_add(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 1);
	ret=OR_map_exisits(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 1);

	const char *key2="lisi";
	const char *val1="rich", *val2="poor";
	ret=OR_map_set_string(map, key2, strlen(key2)+1, val1, strlen(val1)+1);
	EXPECT_EQ(ret, 1);
	struct OR_map_kv *result=NULL;
	result=OR_map_get(map,  key2, strlen(key2)+1);
	EXPECT_STREQ(val1, result->string);
	OR_map_kv_free(result);
	result=NULL;
	
	ret=OR_map_set_string(map, key2, strlen(key2)+1, val2, strlen(val2)+1);
	EXPECT_EQ(ret, 0);
	result=OR_map_get(map,  key2, strlen(key2)+1);
	EXPECT_STREQ(val2, result->string);
	OR_map_kv_free(result);
	result=NULL;

	ret=OR_map_set_string(map, key1, strlen(key1)+1, val1, strlen(val1)+1);
	EXPECT_EQ(ret, 0);
	long long integer_result=0;
	ret=OR_map_incrby(map, key1, strlen(key1)+1, 0, &integer_result);
	EXPECT_EQ(ret, -1);

	const char *key3="Jack";
	ret=OR_map_incrby(map, key3, strlen(key3)+1, 0, &integer_result);
	EXPECT_EQ(ret, 1);
	EXPECT_EQ(integer_result, 0);
	ret=OR_map_incrby(map, key3, strlen(key3)+1, -100, &integer_result);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(integer_result, -100);
	ret=OR_map_incrby(map, key3, strlen(key3)+1, 200, &integer_result);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(integer_result, 100);

	ret=OR_map_remove(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 1);
	ret=OR_map_exisits(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 0);
	struct OR_map_kv **kv_list=NULL;
	size_t n_kv=0;
	n_kv=OR_map_cardinality(map);
	EXPECT_EQ(n_kv, 2);
	
	n_kv=OR_map_keys(map, &kv_list);
	EXPECT_EQ(n_kv, 2);
	
	for(size_t i=0; i<n_kv; i++)
	{
		ret=OR_map_exisits(map, kv_list[i]->key, kv_list[i]->key_len);
		EXPECT_EQ(ret, 1);
		EXPECT_TRUE(kv_list[i]->string==NULL);//OR_map_keys() doesn't return value.
		OR_map_kv_free(kv_list[i]);
		kv_list[i]=NULL;
	}
	free(kv_list);

	OR_map_free(map);
	
}
int my_key_filter(const char *key, size_t key_len, void *cb_arg)
{
	const char *prefix=(const char *)cb_arg;
	if(0==strncmp(key, prefix, strlen(prefix)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

TEST(ORMap, RepetitiveGet)
{
	uuid_t uuid;
	uuid_generate(uuid);
	int ret = 0;
	struct OR_map *map=OR_map_new(uuid);
	int KV_NUM = 1000, ROUND=1000;
	uuid_t keys[KV_NUM];
	uuid_t values[KV_NUM];

	struct OR_map_kv *result=NULL;
	for(int i=0; i<KV_NUM; i++)
	{
		uuid_generate(keys[i]);
		uuid_generate(values[i]);
		ret = OR_map_set_string(map, (const char*)&keys[i], sizeof(keys[i]), (const char*)values[i], sizeof(values[i]));
		EXPECT_EQ(ret, 1);
	}
	for(int j=0; j<ROUND; j++)
	{
		for(int i=0; i<KV_NUM; i++)
		{
			result=OR_map_get(map,  (const char*)&keys[i], sizeof(keys[i]));
			ASSERT_TRUE(result);
			ASSERT_EQ(result->type, TYPE_STRING);
			EXPECT_EQ(memcmp(result->string, values[i], sizeof(values[i])), 0);
			OR_map_kv_free(result);
			result = NULL;
		}
	}

	OR_map_free(map);
}

TEST(ORMap, Filter)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct OR_map *map=OR_map_new(uuid);
	int ret=0;
	char key[32];
	long long value=0;
	size_t n_prefixed_key=8;
	const char *prefix="a_";
	for(size_t i=0; i<n_prefixed_key; i++)
	{
		snprintf(key, sizeof(key), "%s%zu", prefix, i);
		value=i;
		ret=OR_map_set_integer(map, key, strlen(key)+1, value);
	}
	for(size_t i=0; i<12; i++)
	{
		snprintf(key, sizeof(key), "b_%zu", i);
		value=i;
		ret=OR_map_set_integer(map, key, strlen(key)+1, value);
	}

	struct OR_map_kv **kv_list=NULL;
	size_t n_kv=0;
	n_kv=OR_map_filter_keys(map, my_key_filter, (void*)prefix, &kv_list);
	EXPECT_EQ(n_kv, n_prefixed_key);
	for(size_t i=0; i<n_kv; i++)
	{
		ret=strncmp(kv_list[i]->key, prefix, strlen(prefix));
		EXPECT_EQ(ret, 0);
		OR_map_kv_free(kv_list[i]);
		kv_list[i]=NULL;
	}
	free(kv_list);

	OR_map_free(map);
}
TEST(ORMap, Merge)
{
	uuid_t uuid;
	size_t i=0;
	struct OR_map *map[2];

	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	const char *key="Tom";
	long long result=0;
	OR_map_incrby(map[0], key, strlen(key)+1, 100, &result);
	OR_map_incrby(map[1], key, strlen(key)+1, 200, &result);

	char *blob=NULL;
	size_t blob_sz=0;
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);

	OR_map_incrby(map[0], key, strlen(key)+1, 0, &result);
	EXPECT_EQ(result, 300);

	OR_map_incrby(map[1], key, strlen(key)+1, 0, &result);
	EXPECT_EQ(result, 300);
	
	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}

}
TEST(ORMap, SetGet)
{
	uuid_t uuid;
	size_t i=0;
	const char *key_prefix="OR-map-key";
	const char *val_prefix="OR-map-val";
	struct OR_map *map[2];
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	size_t round=100;
	char key_buffer[256], val_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);
		OR_map_set_string(map[0], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
	}
	OR_map_sync(map, 2);
	struct OR_map_kv *kv_result;
	size_t success_cnt=0;
	for(i=0; i<round; i++)
	{		
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);
		kv_result=OR_map_get(map[1], key_buffer, strlen(key_buffer)+1);
		if(kv_result &&
			kv_result->type==TYPE_STRING && kv_result->str_len==strlen(val_buffer)+1 &&
			0==memcmp(kv_result->string, val_buffer, kv_result->str_len) )
		{
			success_cnt++;
		}
		OR_map_kv_free(kv_result);
	}
	EXPECT_EQ(success_cnt, round);
	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}
}
TEST(ORMap, Incrby)
{
	uuid_t uuid;
	size_t i=0;
	long long result_integer=0;
	const char *key_prefix="OR-map-key";
	struct OR_map *map[2];
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	size_t round=100;
	char key_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		OR_map_incrby(map[i%2], key_buffer, strlen(key_buffer)+1, i, &result_integer);
	}

	OR_map_sync(map, 2);

	struct OR_map_kv *result;
	size_t success_cnt=0;
	for(i=0; i<round; i++)
	{		
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		result=OR_map_get(map[i%2], key_buffer, strlen(key_buffer)+1);
		if(result &&
			result->type==TYPE_INTEGER && 
			result->integer==(long long)i)
		{
			success_cnt++;
		}
		OR_map_kv_free(result);
	}
	EXPECT_EQ(success_cnt, round);

	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		OR_map_incrby(map[i%2], key_buffer, strlen(key_buffer)+1, 998, &result_integer);
	}
	OR_map_sync(map, 2);
	success_cnt=0;
	for(i=0; i<round; i++)
	{		
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		result=OR_map_get(map[i%2], key_buffer, strlen(key_buffer)+1);
		if(result &&
			result->type==TYPE_INTEGER && 
			result->integer==(long long)i+998)
		{
			success_cnt++;
		}
		OR_map_kv_free(result);
	}
	EXPECT_EQ(success_cnt, round);
	
	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}
}
TEST(ORMap, Incrby5K)
{
	uuid_t uuid;
	size_t i=0;
	struct OR_map *map[2];
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	long long result_integer=0;
	size_t round=5000;
	const char *key="priority-1";
	for(i=0; i<round; i++)
	{
		OR_map_incrby(map[i%2], key, strlen(key), 1, &result_integer);
		OR_map_incrby(map[(i+1)%2], key, strlen(key), -1, &result_integer);
		if(i%100==0) OR_map_sync(map, 2);
	}
	OR_map_sync(map, 2);
	OR_map_incrby(map[0], key, strlen(key), 0, &result_integer);
	EXPECT_EQ(result_integer, 0);
	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}
}
TEST(ORMap, Remove)
{
	uuid_t uuid;
	size_t i=0;
	long long result_integer=0;
	const char *key_prefix="OR-map-key";	
	const char *val_prefix="OR-map-val";
	struct OR_map *map[2];
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	size_t round=100;
	char key_buffer[256], val_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		if(i%3==0)
		{
			OR_map_incrby(map[i%2], key_buffer, strlen(key_buffer)+1, i, &result_integer);
		}
		else
		{
			snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);			
			OR_map_set_string(map[i%2], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
		}
	}
	char *blob=NULL;
	size_t blob_sz=0;
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);
	
	for(i=0; i<round/2; i++)
	{	
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		OR_map_remove(map[i%2], key_buffer, strlen(key_buffer)+1);
	}
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);
	size_t success_cnt=0, n_kv=0;
	int ret=0;
	n_kv=OR_map_cardinality(map[0]);
	EXPECT_EQ(n_kv, round-round/2);
	n_kv=OR_map_cardinality(map[1]);
	EXPECT_EQ(n_kv, round-round/2);
	
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		ret=OR_map_exisits(map[(i+1)%2], key_buffer, strlen(key_buffer)+1);
		if(i<round/2)
		{
			success_cnt += (ret==0?1:0);
		}
		else
		{
			success_cnt += (ret==1?1:1);
		}
	}
	EXPECT_EQ(success_cnt, round);
	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}
}
TEST(ORMap, ObservedRemove)
{
	uuid_t uuid;
	size_t i=0;
	const char *key_prefix="OR-map-key";	
	const char *val_prefix="OR-map-val";
	struct OR_map *map[2];
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	size_t round=100;
	char key_buffer[256], val_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);			
		OR_map_set_string(map[i%2], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
		
	}
	char *blob=NULL;
	size_t blob_sz=0;
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);

	int ret=0;
	ret=OR_map_remove(map[0], key_buffer, strlen(key_buffer)+1);
	EXPECT_EQ(ret, 1);
	const char *val="Hello Stellar!";
	ret=OR_map_set_string(map[1], key_buffer, strlen(key_buffer)+1, val, strlen(val)+1);

	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);

	struct OR_map_kv *kv=NULL;
	kv=OR_map_get(map[0], key_buffer, strlen(key_buffer)+1);
	EXPECT_STREQ(kv->string, val);
	OR_map_kv_free(kv);

	kv=OR_map_get(map[1], key_buffer, strlen(key_buffer)+1);
	EXPECT_STREQ(kv->string, val);
	OR_map_kv_free(kv);

	for(i=0; i<2; i++)
	{
		OR_map_free(map[i]);
	}
}
TEST(ORMap, Replica32)
{
	size_t replica_number=32, kv_number=1024;
	uuid_t uuid;
	size_t i=0, j=0;
	const char *key_prefix="OR-map-key";	
	const char *val_prefix="OR-map-val";
	struct OR_map *map[replica_number];
	for(i=0; i<replica_number; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new(uuid);
	}
	
	char key_buffer[256], val_buffer[256];
	long long result_integer=0;
	for(i=0; i<kv_number; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		switch(i%3)
		{
			case 0:			
				OR_map_incrby(map[i%replica_number], key_buffer, strlen(key_buffer)+1, i, &result_integer);
				break;
			case 1:
				snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);			
				OR_map_set_string(map[i%replica_number], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
				break;
			case 2:
				OR_map_add(map[i%replica_number], key_buffer, strlen(key_buffer)+1);
				break;
			default:
				break;
		}
	}
	char* blob=NULL;
	size_t blob_sz=0;
	for(i=0; i<replica_number; i++)
	{
		OR_map_serialize(map[i], &blob, &blob_sz);
		for(j=0; j<replica_number; j++)
		{
			OR_map_merge_blob(map[j], blob, blob_sz);
		}
		free(blob);
	}
	struct OR_map_kv *kv_result=NULL;
	for(i=0; i<kv_number; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		kv_result=OR_map_get(map[(i+1)%replica_number], key_buffer, strlen(key_buffer)+1);
		switch(i%3)
		{
			case 0:			
				EXPECT_EQ(kv_result->type, TYPE_INTEGER);
				EXPECT_EQ(kv_result->integer, i);
				break;
			case 1:
				snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);
				EXPECT_EQ(kv_result->type, TYPE_STRING);
				EXPECT_STREQ(kv_result->string, val_buffer);
				break;
			case 2:
				EXPECT_EQ(kv_result->type, TYPE_NONE);
				break;
			default:
				break;
		}
		OR_map_kv_free(kv_result);
		kv_result=NULL;
	}	
	for(i=0; i<kv_number; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		switch(i%3)
		{
			case 0: 		
				OR_map_incrby(map[i%replica_number], key_buffer, strlen(key_buffer)+1, i, &result_integer);
				break;
			case 1:
				snprintf(val_buffer, sizeof(val_buffer), "%s-%zu-updated", val_prefix, i);			
				OR_map_set_string(map[i%replica_number], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
				break;
			case 2:
				OR_map_remove(map[i%replica_number], key_buffer, strlen(key_buffer)+1);
				break;
			default:
				break;
		}

	}
	for(i=0; i<replica_number; i++)
	{
		OR_map_serialize(map[i], &blob, &blob_sz);
		for(j=0; j<replica_number; j++)
		{
			OR_map_merge_blob(map[j], blob, blob_sz);
		}
		free(blob);
	}
	for(i=0; i<kv_number; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		kv_result=OR_map_get(map[(i+1)%replica_number], key_buffer, strlen(key_buffer)+1);
		switch(i%3)
		{
			case 0: 		
				EXPECT_EQ(kv_result->integer, i*2);
				break;
			case 1:
				snprintf(val_buffer, sizeof(val_buffer), "%s-%zu-updated", val_prefix, i);			
				EXPECT_STREQ(kv_result->string, val_buffer);
				break;
			case 2:
				EXPECT_TRUE(kv_result==NULL);
				break;
			default:
				break;
		}
		OR_map_kv_free(kv_result);
		kv_result=NULL;
	}

	for(i=0; i<replica_number; i++)
	{
		OR_map_free(map[i]);
	}

}

void g_array_sync(struct g_array **replicas, size_t n_replica)
{
	char *blob=NULL;
	size_t blob_sz=0;
	for(size_t i=0; i<n_replica; i++)
	{
		for(size_t j=0; j<n_replica; j++)
		{
			if(i==j) continue;
			g_array_serialize(replicas[j], &blob, &blob_sz);
			g_array_merge_blob(replicas[i], blob, blob_sz);
			free(blob);
		}
	}
	return;
}

TEST(GArray, Basic)
{
	uuid_t uuid;
	size_t n_replica=4;
	int array_sz=1024;
	struct g_array *a[n_replica];
	for(size_t i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		a[i]=g_array_new(uuid, array_sz);
	}
	int r=0, round=100;
	long long increment=77;
	for(int i=0; i<round; i++)
	{
		r=random()%n_replica;
		for(int j=0; j<array_sz; j++)
		{
			g_array_incrby(a[r], j, increment);
		}
		if(i%7==0)
		{
			g_array_sync(a, n_replica);
		}
	}
	g_array_sync(a, n_replica);
	g_array_sync(a, n_replica);
	
	long long value=0, success=0;
	for(int i=0; i<array_sz; i++)
	{
		r=random()%n_replica;
		value=g_array_get(a[r], i);
		if(value == (long long)round*increment) success++;
	}
	EXPECT_EQ(success, array_sz);
	g_array_reset(a[0]);
	value=g_array_get(a[0], 0);
	EXPECT_EQ(value, 0);
	g_array_sync(a, n_replica);
	r=random()%n_replica;
	value=g_array_get(a[r], 0);
	EXPECT_EQ(value, 0);

	for(size_t i=0; i<n_replica; i++)
	{
		g_array_free(a[i]);
	}
}
TEST(GArray, Reset)
{
	uuid_t uuid;
	size_t n_replica=2;
	int array_sz=1024;
	size_t round=100;
	struct g_array *a[n_replica];
	for(size_t i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		a[i]=g_array_new(uuid, array_sz);
	}
	long long idx=1, value=0;
	srandom(17);
	for(size_t i=0; i<round; i++)
	{
		g_array_incrby(a[0], idx, 100);
		g_array_incrby(a[1], idx, 100);
		g_array_sync(a, n_replica);
		value=g_array_get(a[0], idx);
		EXPECT_EQ(value, 200);
		g_array_reset(a[0]);
		value=g_array_get(a[0], idx);
		EXPECT_EQ(value, 0);
		g_array_incrby(a[1], idx, 1);
		g_array_sync(a, n_replica);
		//Observed Reset
		value=g_array_get(a[0], idx);
		EXPECT_EQ(value, 1);
		value=g_array_get(a[1], idx);
		EXPECT_EQ(value, 1);
		idx++;
	}


	for(size_t i=0; i<n_replica; i++)
	{
		g_array_free(a[i]);
	}
}
TEST(GArray, Serialize)
{
	uuid_t uuid;
	int n_replica=4;
	int array_sz=1024;
	struct g_array *a[n_replica], *a2=NULL;
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		a[i]=g_array_new(uuid, array_sz);
		g_array_incrby(a[i], 0, 30);
	}
	g_array_sync(a, n_replica);

	size_t sz=g_array_serialized_size(a[0]);
	char *blob=NULL;
	size_t blob_sz=0;
	g_array_serialize(a[0], &blob, &blob_sz);
	EXPECT_EQ(sz, blob_sz);
	a2=g_array_deserialize(blob, blob_sz);
	sz=g_array_serialized_size(a2);
	EXPECT_EQ(sz, blob_sz);
	free(blob);
	for(int i=0; i<n_replica; i++)
	{
		g_array_free(a[i]);
	}
	g_array_free(a2);
}
TEST(GArray, Merge)
{
	uuid_t uuid;
	int n_replica=2;
	int array_sz=1024;
	struct g_array *a[n_replica];
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		a[i]=g_array_new(uuid, array_sz*(i+1));
		g_array_incrby(a[i], 0, 30);
	}
	g_array_sync(a, n_replica);
	long long value=0;
	value=g_array_get(a[0], 0);
	EXPECT_EQ(value, 30*n_replica);
	value=g_array_incrby(a[0], array_sz*n_replica-1, 1);
	EXPECT_EQ(value, 1);
	for(int i=0; i<n_replica; i++)
	{
		g_array_free(a[i]);
	}
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

