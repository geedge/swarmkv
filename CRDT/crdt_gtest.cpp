#include "lww_register.h"
#include "pn_counter.h"
#include "or_set.h"
#include "or_map.h"

#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>

#define NDOE_NUMBER	8

TEST(LWWRegister, Basic)
{
	struct LWW_register *reg=NULL;
	uuid_t uuid;
	uuid_generate(uuid);
	reg=LWW_register_new((const char*) uuid, sizeof(uuid));
	
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
		reg[i]=LWW_register_new((const char*)uuid, sizeof(uuid));
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
		reg[i]=LWW_register_new((const char*)uuid, sizeof(uuid));
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
	pnc=PN_counter_new((const char*)uuid, sizeof(uuid));
	value=PN_counter_get(pnc);
	EXPECT_EQ(value, 0);

	value=PN_counter_incrby(pnc, 100);
	EXPECT_EQ(value, 100);

	value=PN_counter_incrby(pnc, -20);
	EXPECT_EQ(value, 80);

	value=PN_counter_reset(pnc);
	EXPECT_EQ(value, 0);

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
		pnc[i]=PN_counter_new((const char*)uuid, sizeof(uuid));
	}
	long long value;
	PN_counter_incrby(pnc[0], 200);
	PN_counter_incrby(pnc[1], -100);

	PN_counter_sync(pnc, 2);

	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 100);
	
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 100);

	PN_counter_reset(pnc[0]);
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
		pnc[i]=PN_counter_new((const char*)uuid, sizeof(uuid));
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
		pnc[i]=PN_counter_new((const char*)uuid, sizeof(uuid));
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
	struct OR_set *set=OR_set_new((const char*)uuid, sizeof(uuid));
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
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
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
TEST(ORSet,   Merge11)
{
	struct OR_set *set[2];
	uuid_t uuid;
	size_t i=0;

	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
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
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
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
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
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
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
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
TEST(ORMap, Basic)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct OR_map *map=OR_map_new((const char *)uuid, sizeof(uuid));

	const char *key1="zhangsan";
	int ret=0;
	ret=OR_map_add(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 1);
	ret=OR_map_exisits(map, key1, strlen(key1)+1);
	EXPECT_EQ(ret, 1);

	const char *key2="lisi";
	const char *val1="rich", *val2="poor";
	ret=OR_map_set(map, key2, strlen(key2)+1, val1, strlen(val1)+1);
	EXPECT_EQ(ret, 1);
	struct OR_map_kv *result=NULL;
	result=OR_map_get(map,  key2, strlen(key2)+1);
	EXPECT_STREQ(val1, result->string);
	OR_map_kv_free(result);
	result=NULL;
	
	ret=OR_map_set(map, key2, strlen(key2)+1, val2, strlen(val2)+1);
	EXPECT_EQ(ret, 0);
	result=OR_map_get(map,  key2, strlen(key2)+1);
	EXPECT_STREQ(val2, result->string);
	OR_map_kv_free(result);
	result=NULL;

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
TEST(ORMap, Merge)
{
	uuid_t uuid;
	size_t i=0;
	struct OR_map *map[2];

	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
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
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
	}
	size_t round=100;
	char key_buffer[256], val_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);
		OR_map_set(map[0], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
	}
	char *blob=NULL;
	size_t blob_sz=0;
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
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
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
	}
	size_t round=100;
	char key_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		OR_map_incrby(map[i%2], key_buffer, strlen(key_buffer)+1, i, &result_integer);
	}
	char *blob=NULL;
	size_t blob_sz=0;
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);

	struct OR_map_kv *result;
	size_t success_cnt=0;
	for(i=0; i<round; i++)
	{		
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		result=OR_map_get(map[i%2], key_buffer, strlen(key_buffer)+1);
		if(result &&
			result->type==TYPE_COUNTER && 
			result->counter==(long long)i)
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
	OR_map_serialize(map[0], &blob, &blob_sz);
	OR_map_merge_blob(map[1], blob, blob_sz);
	free(blob);
	OR_map_serialize(map[1], &blob, &blob_sz);
	OR_map_merge_blob(map[0], blob, blob_sz);
	free(blob);

	success_cnt=0;
	for(i=0; i<round; i++)
	{		
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		result=OR_map_get(map[i%2], key_buffer, strlen(key_buffer)+1);
		if(result &&
			result->type==TYPE_COUNTER && 
			result->counter==(long long)i+998)
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
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
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
			OR_map_set(map[i%2], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
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
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
	}
	size_t round=100;
	char key_buffer[256], val_buffer[256];
	for(i=0; i<round; i++)
	{
		snprintf(key_buffer, sizeof(key_buffer), "%s-%zu", key_prefix, i);
		snprintf(val_buffer, sizeof(val_buffer), "%s-%zu", val_prefix, i);			
		OR_map_set(map[i%2], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
		
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
	ret=OR_map_set(map[1], key_buffer, strlen(key_buffer)+1, val, strlen(val)+1);

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
		map[i]=OR_map_new((const char *)uuid, sizeof(uuid));
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
				OR_map_set(map[i%replica_number], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
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
				EXPECT_EQ(kv_result->type, TYPE_COUNTER);
				EXPECT_EQ(kv_result->counter, i);
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
				OR_map_set(map[i%replica_number], key_buffer, strlen(key_buffer)+1, val_buffer, strlen(val_buffer)+1);
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
				EXPECT_EQ(kv_result->counter, i*2);
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

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

