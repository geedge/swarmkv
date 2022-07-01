#include "lww_register.h"
#include "pn_counter.h"
#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>

#define NDOE_NUMBER	8

TEST(LastWriteWinRegister, Basic)
{
	struct lww_register *reg=NULL;
	reg=lww_register_new(NULL, 0);
	
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
	for(size_t i=0; i<2; i++)
	{
		reg[i]=lww_register_new(NULL, 0);
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
	for(size_t i=0; i<2; i++)
	{
		reg[i]=lww_register_new(NULL, 0);
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
TEST(PNCounter, Nodes300)
{
	size_t test_node_num=300, i=0, j=0;	
	uuid_t uuid;

	struct PN_counter *pnc[test_node_num];
	for(i=0; i<test_node_num; i++)
	{
		uuid_generate(uuid);
		pnc[i]=PN_counter_new((const char*)uuid, sizeof(uuid));
	}
	long long expect_value=0, actual_value=0, increment=0;
	srand(171);
	size_t node_idx=0, loop=100000;
	for(i=0; i<loop; i++)
	{
		node_idx=rand()%test_node_num;
		increment=2*rand();//to generate negtive expect_value
		PN_counter_incrby(pnc[node_idx], increment);
		expect_value+=increment;
		
	}

	char *blob=NULL;
	size_t blob_sz=0;
	
	for(i=0; i<test_node_num; i++)
	{
		PN_counter_serialize(pnc[i], &blob, &blob_sz);
		for(j=0; j<test_node_num; j++)
		{
			PN_counter_merge(pnc[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	size_t success=0;
	for(i=0; i<test_node_num; i++)
	{
		actual_value=PN_counter_get(pnc[i]);
		if(expect_value==actual_value)
		{
			success++;
		}
	}
	EXPECT_EQ(success, test_node_num);
	
	for(i=0; i<test_node_num; i++)
	{
		PN_counter_free(pnc[i]);
	}	
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}

