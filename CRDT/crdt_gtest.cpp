#include "lww_register.h"
#include "pn_counter.h"
#include "or_set.h"

#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>

#define NDOE_NUMBER	8

TEST(LastWriteWinRegister, Basic)
{
	struct lww_register *reg=NULL;
	uuid_t uuid;
	uuid_generate(uuid);
	reg=lww_register_new((const char*) uuid, sizeof(uuid));
	
	const char *value0=NULL;
	size_t val_sz=0;
	lww_register_get0(reg, &value0, &val_sz);
	EXPECT_TRUE(value0==NULL);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "abc123");
	lww_register_set(reg, tmp, strlen(tmp)+1);
	lww_register_get0(reg, &value0, &val_sz);
	EXPECT_STREQ(tmp, value0);
	value0=NULL;

	char *value1=NULL;
	snprintf(tmp, sizeof(tmp), "abcdefg");
	lww_register_set(reg, tmp, strlen(tmp)+1);
	lww_register_get1(reg, &value1, &val_sz);
	EXPECT_STREQ(tmp, value1);
	free(value1);

	lww_register_free(reg);
		
	return;
}
TEST(LastWriteWinRegister, Merge)
{
	struct lww_register *reg[2];	
	uuid_t uuid;

	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		reg[i]=lww_register_new((const char*)uuid, sizeof(uuid));
	}
	const char *value0=NULL;
	size_t val_sz=0;
	lww_register_get0(reg[0], &value0, &val_sz);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "abc123");
	lww_register_set(reg[0], tmp, strlen(tmp)+1);
	EXPECT_TRUE(value0==NULL);

	char *blob=NULL;
	size_t blob_sz=0;
	lww_register_serialize(reg[0], &blob, &blob_sz);
	lww_register_merge(reg[1], blob, blob_sz);
	free(blob);
	
	char *value1=NULL;	
	lww_register_get1(reg[1], &value1, &val_sz);
	EXPECT_STREQ(tmp, value1);
	free(value1);

	for(size_t i=0; i<2; i++)
	{
		lww_register_free(reg[i]);
	}	
	return;
}
TEST(LastWriteWinRegister, Sequential)
{
	struct lww_register *reg[2];
	uuid_t uuid;
	for(size_t i=0; i<2; i++)
	{
		uuid_generate(uuid);
		reg[i]=lww_register_new((const char*)uuid, sizeof(uuid));
	}
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "reg1:abc123");
	lww_register_set(reg[0], tmp, strlen(tmp)+1);
	usleep(10);
	
	snprintf(tmp, sizeof(tmp), "reg2:abcdefg");
	lww_register_set(reg[1], tmp, strlen(tmp)+1);


	char *blob=NULL;
	size_t blob_sz=0;
	lww_register_serialize(reg[0], &blob, &blob_sz);
	lww_register_merge(reg[1], blob, blob_sz);
	free(blob);

	lww_register_serialize(reg[1], &blob, &blob_sz);
	lww_register_merge(reg[0], blob, blob_sz);
	free(blob);

	char *value=NULL;
	size_t val_sz=0;
	lww_register_get1(reg[0], &value, &val_sz);
	EXPECT_STREQ(tmp, value);
	free(value);
	
	lww_register_get1(reg[1], &value, &val_sz);
	EXPECT_STREQ(tmp, value);
	free(value);

	for(size_t i=0; i<2; i++)
	{
		lww_register_free(reg[i]);
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

	char *blob=NULL;
	size_t blob_sz=0;

	PN_counter_serialize(pnc[0], &blob, &blob_sz);
	PN_counter_merge(pnc[1], blob, blob_sz);
	free(blob);
	
	PN_counter_serialize(pnc[1], &blob, &blob_sz);
	PN_counter_merge(pnc[0], blob, blob_sz);
	free(blob);

	value=PN_counter_get(pnc[0]);
	EXPECT_EQ(value, 100);
	
	value=PN_counter_get(pnc[1]);
	EXPECT_EQ(value, 100);
	
	for(size_t i=0; i<2; i++)
	{
		PN_counter_free(pnc[i]);
	}

}
TEST(PNCounter, Replica100)
{
	size_t test_replica_num=100, i=0, j=0;	
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

	char *blob=NULL;
	size_t blob_sz=0;
	
	for(i=0; i<test_replica_num; i++)
	{
		PN_counter_serialize(pnc[i], &blob, &blob_sz);
		for(j=0; j<test_replica_num; j++)
		{
			PN_counter_merge(pnc[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
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
	OR_set_merge(set[1], blob, blob_sz);
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
	OR_set_merge(set[1], blob, blob_sz);
	free(blob);

	OR_set_serialize(set[1], &blob, &blob_sz);
	OR_set_merge(set[0], blob, blob_sz);
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
	OR_set_merge(set[1], blob, blob_sz);
	free(blob);
	
	int is_member=0;
	snprintf(buffer, sizeof(buffer), "ORSet-Member-0");
	is_member=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 1);
	
	OR_set_remove(set[0], buffer, strlen(buffer)+1);
	OR_set_serialize(set[0], &blob, &blob_sz);
	OR_set_merge(set[1], blob, blob_sz);
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
	OR_set_merge(set[1], blob, blob_sz);
	free(blob);

	is_member=OR_set_is_member(set[1], buffer, strlen(buffer)+1);
	EXPECT_EQ(is_member, 1);

	
	OR_set_serialize(set[1], &blob, &blob_sz);
	OR_set_merge(set[0], blob, blob_sz);
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
	size_t test_replica_num=4, test_member_num=4096;
	struct OR_set *set[test_replica_num];
	uuid_t uuid;
	size_t i=0, j=0;
	for(i=0; i<test_replica_num; i++)
	{
		uuid_generate(uuid);
		set[i]=OR_set_new((const char*)uuid, sizeof(uuid));
	}
	char buffer[256];
	for(i=0; i<test_member_num; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		OR_set_add(set[i%test_replica_num], buffer, strlen(buffer)+1);
	}
	
	char *blob=NULL;
	size_t blob_sz=0;
	for(i=0; i<test_replica_num; i++)
	{
		OR_set_serialize(set[i], &blob, &blob_sz);
		for(j=0; j<test_replica_num; j++)
		{
			OR_set_merge(set[j], blob, blob_sz);
		}
		free(blob);
		blob_sz=0;
	}
	size_t success_cnt=0;
	for(i=0; i<test_member_num; i++)
	{
		snprintf(buffer, sizeof(buffer), "ORSet-Member-%zu", i);
		for(j=0; j<test_replica_num; j++)
		{
			success_cnt+=OR_set_is_member(set[j], buffer, strlen(buffer)+1);
		}
	}
	EXPECT_EQ(success_cnt, test_member_num*test_replica_num);
	size_t n_replica=0;
	success_cnt=0;
	for(i=0; i<test_replica_num; i++)
	{
		n_replica=OR_set_replica_number(set[i]);
		if(n_replica==test_replica_num)
		{
			success_cnt++;
		}
		OR_set_free(set[i]);
		set[i]=NULL;
	}
	EXPECT_EQ(success_cnt, test_replica_num);
}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

