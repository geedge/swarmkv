#include "swarmkv/swarmkv.h"
#include "test_utils.h"
#include "log.h"

#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/time.h>

#define	CMD_EXEC_TIMEOUT_MS	1000*2
struct timeval g_exec_timeout={CMD_EXEC_TIMEOUT_MS/1000, (CMD_EXEC_TIMEOUT_MS%1000)*1000};
void generic_callback(const struct swarmkv_reply *reply, void * cb_arg)
{
	struct cmd_exec_arg *arg=(struct cmd_exec_arg*)cb_arg;
	if(0==reply_compare(reply, &(arg->expected_reply)))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		cmd_exec_arg_failed(arg);
	}
	return;
}
int g_current_thread_id=0;
void copy_reply_callback(const struct swarmkv_reply *reply, void * cb_arg)
{
	struct swarmkv_reply **copy=(struct swarmkv_reply **)cb_arg;
	*copy=swarmkv_reply_dup(reply);
	int curr_tid=syscall(SYS_gettid);
	assert(curr_tid==g_current_thread_id);
	return;
}
class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		const char *log_path="./basic-test.log";
		char *err=NULL;
		const char *cluster_name="swarmkv-basic-test";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210");
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options *opts=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 6210);
		swarmkv_options_set_logger(opts, logger);
		swarmkv_options_set_worker_thread_number(opts, 1);
		swarmkv_options_set_caller_thread_number(opts, 1);
		db=swarmkv_open(opts, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s.\n", err);
			free(err);
		}
		swarmkv_register_thread(db);
	}
	static void TearDownTestCase()
	{
		swarmkv_close(db);		
		log_handle_destroy(logger);
	}	
 	// Some expensive resource shared by all tests.
 	
	static struct log_handle *logger;
	static struct swarmkv *db;
};
struct swarmkv *SwarmkvBasicTest::db;
struct log_handle *SwarmkvBasicTest::logger;
TEST_F(SwarmkvBasicTest, TypeString)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="name";
	const char *val="zhangsan";

	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_command(db, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val);
	swarmkv_reply_free(reply);

}
TEST_F(SwarmkvBasicTest, TypeInteger)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="int1";
	struct swarmkv_reply * reply=NULL;
	
	reply=swarmkv_command(db, "INCR %s", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "INCRBY %s 100", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 101);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "INCRBY %s -100", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DECR %s", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	long long value=0;
	for(size_t i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "INCR %s", key);
		value=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(value, 100);
}
TEST_F(SwarmkvBasicTest, GenericDEL)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char* key="name2";
	const char* val="zhangsan";

	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, GenericTYPE)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	struct swarmkv_reply *reply=NULL;

	//TYPE string
	const char *string_key="string-key";
	reply=swarmkv_command(db, "SET %s abc", string_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", string_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "string");
	swarmkv_reply_free(reply);

	//type integer
	const char *integer_key="integer-key";
	reply=swarmkv_command(db, "SET %s 123", integer_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", integer_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "integer");
	swarmkv_reply_free(reply);

	//type set
	const char *set_key="set-key";
	reply=swarmkv_command(db, "SADD %s a b c d", set_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 4);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", set_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "set");
	swarmkv_reply_free(reply);

	//type token-bucket
	const char *tb_key="tb-key";
	reply=swarmkv_command(db, "TCFG %s 4000 2000", tb_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", tb_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "token-bucket");
	swarmkv_reply_free(reply);

	//type hash
	const char *hash_key="hash-key";
	reply=swarmkv_command(db, "HSET %s name zhangsan gender male age 18 gender male", hash_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 3);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", hash_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "hash");
	swarmkv_reply_free(reply);

	//type non-exist key
	const char *nonexist_key="non-exist-key";
	reply=swarmkv_command(db, "type %s", nonexist_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "none");
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeSet)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="friends_of_Jack";
	const char *member[]={"zhangsan", "lisi", "王二麻子", "Tom", "مرحبا"};
	size_t i=0;
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db, "SADD %s %s %s %s %s %s", key, member[0], member[1], member[2], member[3], member[4]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SMEMBERS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, 5);
	for(i=0; i<reply->n_element; i++)
	{
		EXPECT_EQ(reply->elements[i]->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->elements[i]->str, member[i]);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SREM %s %s %s", key, member[0], member[1]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 2);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SISMEMBER %s %s", key, member[0]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SISMEMBER %s %s", key, member[2]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SCARD %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 3);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SISMEMBER %s %s", key, member[2]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "SCARD %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

}
TEST_F(SwarmkvBasicTest, TypeTokenBucket)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="tb-192.168.0.1";
	struct swarmkv_reply *reply=NULL;

	long long capacity=1024*4, rate=1024*2, request_tokens=0, allocated_tokens=0;
	reply=swarmkv_command(db, "TCFG %s %lld %lld", key, rate, capacity);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	srand(171);

	int i=0;
	while(now.tv_sec - start.tv_sec<3)
	{
		request_tokens=random()%(2*rate);
		reply=swarmkv_command(db, "TCONSUME %s %lld FLEXIBLE", key, request_tokens);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			allocated_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	EXPECT_LE(allocated_tokens, (now.tv_sec -start.tv_sec)*rate+capacity);	

	//Infinite tokens
	reply=swarmkv_command(db, "TCFG %s 0 0", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	long long t=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "TCONSUME %s 10000", key);
		t+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(t, 10000*i);
	reply=swarmkv_command(db, "TINFO %s", key);
	ASSERT_EQ(reply->n_element, 10);
	EXPECT_EQ(reply->elements[5]->integer, allocated_tokens+t);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeFairTokenBucket)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="3-floor-bandwidth-100Mbps";
	long long capacity=200*1024*1024, rate=100*1024*1024, request_tokens=0, allocated_tokens=0;

	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_command(db, "FTCFG %s %lld %lld 128", key, rate, capacity);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	srand(171);
	int i=0;
	while(now.tv_sec - start.tv_sec<3)
	{
		request_tokens=random()%(2*rate);
		reply=swarmkv_command(db, "FTCONSUME %s user-001 5 %lld", key, request_tokens);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			allocated_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	EXPECT_LE(allocated_tokens, (now.tv_sec -start.tv_sec)*rate+capacity);	


	//Infinite tokens
	reply=swarmkv_command(db, "FTCFG %s 0 0 256", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	long long t=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "FTCONSUME %s user-001 5 10000", key);
		t+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(t, 10000*i);
	reply=swarmkv_command(db, "FTINFO %s", key);
	ASSERT_EQ(reply->n_element, 14);
	EXPECT_EQ(reply->elements[5]->integer, allocated_tokens+t);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeBulkTokenBucket)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="everyone-has-1Mbps";

	long long capacity=2*1024*1024, rate=1*1024*1024, request_tokens=0, allocated_tokens=0;

	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_command(db, "BTCFG %s %lld %lld 128", key, rate, capacity);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	srand(171);
	int i=0, n_member=120;
	while(now.tv_sec - start.tv_sec<3)
	{
		request_tokens=random()%(2*rate);
		reply=swarmkv_command(db, "BTCONSUME %s user-%d %lld", key, i%n_member, request_tokens);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			allocated_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	EXPECT_LE(allocated_tokens/n_member, (now.tv_sec -start.tv_sec)*rate+capacity);	

	//Infinite tokens
	reply=swarmkv_command(db, "BTCFG %s 0 0 256", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	long long t=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "BTCONSUME %s user-001 10000", key);
		t+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(t, 10000*i);
	reply=swarmkv_command(db, "BTINFO %s", key);
	ASSERT_EQ(reply->n_element, 12);
	EXPECT_NEAR(reply->elements[7]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeHash)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="uid001";
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db, "HSET %s name zhangsan gender male age 18 gender male", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 3);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HGET %s name", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "zhangsan");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HMGET %s name gender not-exist", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 3);
	EXPECT_STREQ(reply->elements[0]->str, "zhangsan");
	EXPECT_STREQ(reply->elements[1]->str, "male");
	EXPECT_EQ(reply->elements[2]->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HINCRBY %s age 1", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 19);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HGET %s age", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "19");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HKEYS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 3);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HGETALL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 6);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HDEL %s gender", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HLEN %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 2);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HGET %s name", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	const char *key2="uid002";
	reply=swarmkv_command(db, "HINCRBY %s age 30", key2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 30);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "HINCRBY %s age -1", key2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 30-1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, EXPIRE_TTL)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="quarantine";
	const char *val="wuhan-江夏-如家";
	int seconds=3;
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "EXPIRE %s %d", key, seconds);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, seconds);
	swarmkv_reply_free(reply);

	sleep(seconds+1);

	reply=swarmkv_command(db, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "EXPIRE %s %d", key, seconds);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -2);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, HashTags)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	struct swarmkv_reply *reply=NULL;
	const char *keys[]={"{user1000}.following", "{user1000}.followers", "{user1000}.{user1001}.watched"};
	int slot_id=-1;
	for(size_t i=0; i<sizeof(keys)/sizeof(const char *); i++)
	{
		reply=swarmkv_command(db, "keyslot %s", keys[i]);
		EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		if(slot_id>=0)
		{
			EXPECT_EQ(reply->integer, slot_id);
		}
		slot_id=reply->integer;
		swarmkv_reply_free(reply);
	}
}
class SwarmkvTwoNodes : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		
		char *err=NULL;
		const char *log_path="./swarmkv-2-nodes.log";
		
		const char *cluster_name="swarmkv-2-nodes";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210 127.0.0.1:5211");
		
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts1=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts1, 5210);
		swarmkv_options_set_health_check_port(opts1, 6210);
		swarmkv_options_set_logger(opts1, logger);
		swarmkv_options_set_sync_interval_us(opts1, 10*1000);
		swarmkv_options_set_cluster_timeout_us(opts1, 600*1000*1000);
		swarmkv_options_set_worker_thread_number(opts1, 1);
		swarmkv_options_set_caller_thread_number(opts1, 1);
		db1=swarmkv_open(opts1, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db1);
		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts2, 5211);
		swarmkv_options_set_health_check_port(opts2, 6211);
		swarmkv_options_set_logger(opts2, logger);
		swarmkv_options_set_cluster_timeout_us(opts2, 600*1000*1000);
		swarmkv_options_set_sync_interval_us(opts2, 10*1000);
		swarmkv_options_set_worker_thread_number(opts2, 1);
		swarmkv_options_set_caller_thread_number(opts2, 1);
		db2=swarmkv_open(opts2, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db2);
	}
	static void TearDownTestCase()
	{
		swarmkv_close(db1);
		swarmkv_close(db2);	
		log_handle_destroy(logger);
	}	
 	// Some expensive resource shared by all tests.
 	static struct log_handle *logger;
	static struct swarmkv *db1, *db2;
};
struct log_handle *SwarmkvTwoNodes::logger;
struct swarmkv *SwarmkvTwoNodes::db1;
struct swarmkv *SwarmkvTwoNodes::db2;
TEST_F(SwarmkvTwoNodes, SET_GET)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db1, "SET %s %s", key, val1);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "SET %s %s", key, val2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val2);
	swarmkv_reply_free(reply);

	wait_for_sync();
	
	reply=swarmkv_command(db1, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val2);
	swarmkv_reply_free(reply);

}
TEST_F(SwarmkvTwoNodes, SET1kString)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	const char *key_prefix="test-1k-string";
	const char *val_prefix="value-xx";
	char key[128]="", val[128]="";
	int i=0, round=100000;
	struct swarmkv_reply *reply=NULL;


	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		snprintf(val, sizeof(val), "%s-%d", val_prefix, i);
		tmp_db=i%2?db1:db2;
		reply=swarmkv_command(tmp_db, "SET %s %s", key, val);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		EXPECT_STREQ(reply->str, "OK");
		swarmkv_reply_free(reply);
	}

	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		snprintf(val, sizeof(val), "%s-%d", val_prefix, i);

		tmp_db=i%2?db2:db1;
		reply=swarmkv_command(tmp_db, "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, val);
		swarmkv_reply_free(reply);

	}
}
TEST_F(SwarmkvTwoNodes, INCRBY1kInteger)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	const char *key_prefix="incrby-1k-integer";
	char key[128]="";
	int i=0, round=1000;
	struct swarmkv_reply *reply=NULL;

	int increment=-3000000;
	for(i=0; i<round; i++)
	{
		tmp_db=i%2?db1:db2;
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		reply=swarmkv_command(tmp_db, "INCRBY %s %d", key, i+increment);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, i+increment);
		swarmkv_reply_free(reply);
	}
	for(i=0; i<round; i++)
	{
		tmp_db=(i+1)%2?db1:db2;
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		reply=swarmkv_command(tmp_db, "INCRBY %s %d", key, 0);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, i+increment);
		swarmkv_reply_free(reply);
	}
	for(i=0; i<round; i++)
	{
		tmp_db=i%2?db1:db2;
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		reply=swarmkv_command(tmp_db, "SET %s 0", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		EXPECT_STREQ(reply->str, "OK");
		swarmkv_reply_free(reply);
	}
	for(i=0; i<round; i++)
	{ 
		tmp_db=(i+1)%2?db1:db2;
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		reply=swarmkv_command(tmp_db, "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, "0");
		swarmkv_reply_free(reply);
	}

}
TEST_F(SwarmkvTwoNodes, HINCRBY5K)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	const char *key="myhash";
	const char *field="priority-0";

	struct swarmkv_reply *reply=NULL;
	int i=0, round=5000;
	
	for(i=0; i<round; i++)
	{
		reply=swarmkv_command(db1, "HINCRBY %s %s 1", key, field);
		swarmkv_reply_free(reply);

		reply=swarmkv_command(db2, "HINCRBY %s %s -1", key, field);
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	reply=swarmkv_command(db2, "HINCRBY %s %s 0", key, field);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvTwoNodes, DEL)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	const char* key="id002";
	const char* val="to-be-deleted";
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db1, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db1, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvTwoNodes, INCRBY)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	const char* key="id003";
	long long val=10000;

	struct swarmkv_reply *reply=NULL;
	
	reply=swarmkv_command(db1, "INCRBY %s %lld", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, val);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "INCRBY %s 100", key);
	val+=100;
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, val);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "INCRBY %s -200", key);
	val+=(-200);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, val);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, EXPIRE)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	char key[128]="", val[128]="";
	int i=0, round=128;
	int max_timeout_seconds=10, min_timeout_seconds=2;
	int seconds=0;

	struct swarmkv_reply *reply=NULL;

	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		snprintf(val, sizeof(val), "value-xx-%d", i);
		tmp_db=i%2?db1:db2;
		reply=swarmkv_command(tmp_db, "SET %s %s", key, val);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		EXPECT_STREQ(reply->str, "OK");
		swarmkv_reply_free(reply);
	}

	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		reply=swarmkv_command(tmp_db, "EXPIRE %s %d", key, seconds);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 1);
		swarmkv_reply_free(reply);
	}

	sleep(max_timeout_seconds+1);

	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		reply=swarmkv_command(tmp_db, "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
		swarmkv_reply_free(reply);
	}

	/*
	 * KEYSPACE RLIST expiring-key-%d
	 * Expect NIL reply, make sure that expired keys are removed from keyspace.
	*/

	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		reply=swarmkv_command(tmp_db, "KEYSPACE RLIST %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
		swarmkv_reply_free(reply);
	}
}
TEST_F(SwarmkvTwoNodes, TTL)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;

	const char *key="ttl-key-001";
	const char *value="hello-world";
	int seconds=3;

	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db1, "SET %s %s", key, value);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "EXPIRE %s %d", key, seconds);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, seconds);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, seconds);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, PERSIST)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;

	const char *key="persit-key-001";
	const char *value="hello-world";
	int seconds=3;

	struct swarmkv_reply *reply=NULL;
	
	reply=swarmkv_command(db1, "SET %s %s", key, value);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "EXPIRE %s %d", key, seconds);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);

	reply=swarmkv_command(db2, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, seconds);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db1, "PERSIST %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db2, "TTL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -1);
	swarmkv_reply_free(reply);

	sleep(seconds+1);

	reply=swarmkv_command(db1, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, value);
	swarmkv_reply_free(reply);

}

TEST_F(SwarmkvTwoNodes, FromLocalReplica)
{
	struct swarmkv *db1=SwarmkvTwoNodes::db1;
	struct swarmkv *db2=SwarmkvTwoNodes::db2;
	const char *key="id004";
	const char *val1="lisi", *val2="wang2mazi";
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_command(db1, "SET %s %s", key, val1);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	
	reply=swarmkv_command(db2, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val1);
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db2, "SET %s %s", key, val2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db1, "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, val2);
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db1, swarmkv_self_address(db1), "CRDT EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command_on(db2, swarmkv_self_address(db2), "CRDT EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, TypeSet)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	const char *key="myset001";
	char **members=NULL;
	size_t *member_len=NULL;
	size_t n_member=128;

	members=ALLOC(char*, n_member);
	member_len=ALLOC(size_t, n_member);
	for(size_t i=0; i<n_member; i++)
	{
		members[i]=ALLOC(char, 1024);
		member_len[i]=snprintf(members[i], 1024, "set-member-%zu", i);
	}
	struct swarmkv_reply *reply=NULL;

	swarmkv_sadd(db[0], key, strlen(key), (const char**) members, member_len, n_member/2, copy_reply_callback, &reply);
	swarmkv_caller_loop(db[0], NULL);
	ASSERT_TRUE(reply!=NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, n_member/2);
	swarmkv_reply_free(reply);
	reply=NULL;

	swarmkv_sadd(db[1], key, strlen(key), (const char**)  members+n_member/2, member_len+n_member/2, n_member-n_member/2, copy_reply_callback, &reply);	
	swarmkv_caller_loop(db[1], NULL);
	ASSERT_TRUE(reply!=NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, n_member-n_member/2);
	swarmkv_reply_free(reply);
	reply=NULL;
	
	wait_for_sync();

	swarmkv_smembers(db[1], key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db[1], NULL);
	ASSERT_TRUE(reply!=NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, n_member);
	swarmkv_reply_free(reply);
	reply=NULL;


	for(size_t i=0; i<n_member; i++)
	{
		if(i%3==0)
		{
			swarmkv_srem(db[i%2], key, strlen(key), (const char**) members+i, member_len+i, 1, copy_reply_callback, &reply);
			swarmkv_caller_loop(db[i%2], NULL);
			ASSERT_TRUE(reply!=NULL);
			ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
			EXPECT_EQ(reply->integer, 1);
			swarmkv_reply_free(reply);
			reply=NULL;
		}
	}

	wait_for_sync();

	for(size_t i=0; i<n_member; i++)
	{
		if(i%3==0)
		{
			swarmkv_sismember(db[(i+1)%2], key, strlen(key), members[i], member_len[i], copy_reply_callback, &reply);
			swarmkv_caller_loop(db[(i+1)%2], NULL);
			ASSERT_TRUE(reply!=NULL);
			ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
			EXPECT_EQ(reply->integer, 0);
			swarmkv_reply_free(reply);
			reply=NULL;
		}
	}

	//Cleanup
	for(size_t i=0; i<n_member; i++)
	{
		free(members[i]);
		members[i]=NULL;
	}
	free(members);
	free(member_len);
}

TEST_F(SwarmkvTwoNodes, TypeTokenBucket)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	const char *key="tb-192.168.0.1";

	struct swarmkv_reply *reply=NULL;

	long long capacity=1024*4, rate=1024*2;

	reply=swarmkv_command(db[0], "TCFG %s %lld %lld", key, rate, capacity);	
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	reply=NULL;


	srandom(171);
	
	int i=0;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, allocated_tokens=0;
	//Two heavy consumers
	while(now.tv_sec - start.tv_sec<10)
	{
		token=random()%(4*rate);
		requested_tokens+=token;
		reply=swarmkv_command(db[i%2], "TCONSUME %s %lld FLEXIBLE", key, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	//Light consumers
	while(0 && now.tv_sec - start.tv_sec<10)
	{
		token=rate*3/4+random()%(rate/2);
		if(i%2==0)
		{
			token=token*2/10;
		}
		else
		{
			token=token*8/10;
		}
		token=token*6/10;
		requested_tokens+=token;
		reply=swarmkv_command(db[0], "TCONSUME %s %lld FLEXIBLE", key, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);
		
		gettimeofday(&now, NULL);
		i++;
	}
	//One heavy consumer
	while(0 && now.tv_sec - start.tv_sec<15)
	{
		token=rate*3/4+random()%(rate);
		requested_tokens+=token;
		reply=swarmkv_command(db[0], "TCONSUME %s %lld", key, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);	
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
//	printf("consume round %d, speed %d ops\n", i, i/(int)(now.tv_sec-start.tv_sec));
	EXPECT_GE(i/(int)(now.tv_sec-start.tv_sec), 100000);
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate+capacity;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);	

	wait_for_sync();

	reply=swarmkv_command(db[0], "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
	reply=NULL;

	wait_for_sync();

	reply=swarmkv_command(db[1], "TCONSUME %s 4096 FLEXIBLE", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -1);
	swarmkv_reply_free(reply);
	reply=NULL;
}
TEST_F(SwarmkvTwoNodes, TypeFairTokenBucket)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	
	const char *key="shaping-profile-with-fairness";
	long long capacity=1024*4, rate=1024*2, divisor=1024;
	struct swarmkv_reply *reply=NULL;
	
	reply=swarmkv_command(db[0], "FTCFG %s %lld %lld %lld", key, rate, capacity, divisor);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	reply=NULL;

	srandom(171);
	
	int round=0;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, allocated_tokens=0;
	long long member_id=0, weight=0, n_member=100;

	while(now.tv_sec - start.tv_sec<20)
	{
		token=random()%(2*rate/n_member);
		requested_tokens+=token;
		
		weight=1+(member_id%20);
		reply=swarmkv_command(db[round%2], "FTCONSUME %s user-%lld %lld %lld", key, member_id, weight, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);

		gettimeofday(&now, NULL);
		member_id=(member_id+1)%n_member;
		round++;
	}
//	printf("consume round %d, speed %d ops\n", i, i/(int)(now.tv_sec-start.tv_sec));
	EXPECT_GE(round/(int)(now.tv_sec-start.tv_sec), 100000);
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate+capacity;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);

	wait_for_sync();

	reply=swarmkv_command(db[0], "FTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 14);
	EXPECT_NEAR(reply->elements[13]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "FTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 14);
	EXPECT_NEAR(reply->elements[13]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db[1], "FTCONSUME %s user-001 20 1000", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, TypeBulkTokenBucket)
{

	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;

	const char *key="shaping-profile-everyone-has-10Mbps";
	long long capacity=15*1024*1024, rate=10*1024*1024, buckets=8192;
	struct swarmkv_reply *reply=NULL;
	
	reply=swarmkv_command(db[0], "BTCFG %s %lld %lld %lld", key, rate, capacity, buckets);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	srandom(171);
	
	int round=0;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, allocated_tokens=0;
	long long member_id=0, n_member=100;

	while(now.tv_sec - start.tv_sec<15)
	{
		token=random()%(2*rate/n_member);
		requested_tokens+=token;
		
		reply=swarmkv_command(db[round%2], "BTCONSUME %s user-%lld %lld", key, member_id, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);

		gettimeofday(&now, NULL);
		member_id=(member_id+1)%n_member;
		round++;
	}
//	printf("consume round %d, speed %d ops\n", i, i/(int)(now.tv_sec-start.tv_sec));
	EXPECT_GE(round/(int)(now.tv_sec-start.tv_sec), 100000);
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate+capacity;
	upper_limit=upper_limit*n_member;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);

	wait_for_sync();

	reply=swarmkv_command(db[0], "BTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 12);
	EXPECT_NEAR(reply->elements[7]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "BTINFO %s user-001", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 12);
	EXPECT_NEAR(reply->elements[7]->integer, n_member, n_member/5);
	EXPECT_LE(reply->elements[9]->integer, capacity);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db[1],"BTCONSUME %s user-001 1000", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -1);
	swarmkv_reply_free(reply);

}
TEST_F(SwarmkvTwoNodes, TypeHash)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;

	size_t n_test_key=100;
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[i%2], "HSET uid-%zu name zhangsan gender male age 18 gender male", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 3);
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[(i+1)%2], "HGET uid-%zu name", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, "zhangsan");
		swarmkv_reply_free(reply);
	}
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[(i+1)%2], "HINCRBY uid-%zu age 1", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 19);
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[(i+1)%2], "HGET uid-%zu age", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, "19");
		swarmkv_reply_free(reply);
	}
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[(i+1)%2], "HSET uid-%zu gender female", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 0);
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[(i+1)%2], "HGET uid-%zu gender", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, "female");
		swarmkv_reply_free(reply);
	}

}
TEST_F(SwarmkvTwoNodes, Wait)
{
	//return;
	//sleep(3600*2);
}
TEST(CloudNative, AnnounceIPPort)
{
	size_t NODE_NUMBER=2, i=0;
	int cluster_port_base=7210;
	int health_check_port_base=8210;
	struct swarmkv *db[NODE_NUMBER];
	struct swarmkv_options *opts[NODE_NUMBER];
	const char *cluster_name="skv-in-k8s";
	char *err=NULL;
	const char *annouce_ip="172.17.58.172";//eth0 of GDNT-BJ-DEV1 of AliCloud
	const char *bind_ip="0.0.0.0";
	char node_list_str[1024]={0};
	for(i=0; i<NODE_NUMBER; i++)
	{
		snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "%s:%zu ", annouce_ip, cluster_port_base+i);
	}
	swarmkv_cli_create_cluster(cluster_name, node_list_str);

	for(i=0; i<NODE_NUMBER; i++)
	{
		opts[i]=swarmkv_options_new();			
		swarmkv_options_set_cluster_port(opts[i], cluster_port_base+i);
		swarmkv_options_set_health_check_port(opts[i], health_check_port_base+i);
		swarmkv_options_set_bind_address(opts[i], bind_ip);
		swarmkv_options_set_cluster_announce_ip(opts[i], annouce_ip);
		db[i]=swarmkv_open(opts[i], cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open %zu instance failed: %s\n", i, err);
			free(err);
			err=NULL;
		}
		swarmkv_register_thread(db[i]);
	}

	const char *key="int1";
	struct swarmkv_reply * reply=NULL;
	
	reply=swarmkv_command(db[0], "INCR %s", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "INCRBY %s 100", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 101);
	swarmkv_reply_free(reply);

	//sleep(10000);

	for(i=0; i<NODE_NUMBER; i++)
	{
		swarmkv_close(db[i]);
	}
}
#define TEST_NODE_NUMBER	2
class SwarmkvNNodes : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{		
		char *err=NULL;
		const char *log_path="./swarmkv-n-nodes.log";
		size_t i=0;		
		char node_list_str[1024]={0};
		for(i=0; i<TEST_NODE_NUMBER; i++)
		{
			snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", 5210+i);
		}
		const char *cluster_name="swarmkv-n-nodes";
		swarmkv_cli_create_cluster(cluster_name, node_list_str);
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options *opts[TEST_NODE_NUMBER];
		for(i=0; i<TEST_NODE_NUMBER; i++)
		{
			opts[i]=swarmkv_options_new();			
			swarmkv_options_set_cluster_port(opts[i], 5210+i);
			swarmkv_options_set_health_check_port(opts[i], 6210+i);
			swarmkv_options_set_worker_thread_number(opts[i], 8);
			swarmkv_options_set_logger(opts[i], logger);
			swarmkv_options_set_cluster_timeout_us(opts[i], 30*1000*1000);
			db[i]=swarmkv_open(opts[i], cluster_name, &err);
			if(err)
			{
				printf("swarmkv_open %zu instance failed: %s\n", i, err);
				free(err);
				err=NULL;
			}
			swarmkv_register_thread(db[i]);
		}

	}
	static void TearDownTestCase()
	{
		for(size_t i=0; i<TEST_NODE_NUMBER; i++)
		{
			swarmkv_close(db[i]);
		}
		log_handle_destroy(logger);
	}	
 	// Some expensive resource shared by all tests. 	
	static struct log_handle * logger;
	static struct swarmkv *db[TEST_NODE_NUMBER];
};
struct swarmkv* SwarmkvNNodes::db[TEST_NODE_NUMBER];
struct log_handle * SwarmkvNNodes::logger;
TEST_F(SwarmkvNNodes, SET_GET)
{
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	struct swarmkv **db=SwarmkvNNodes::db;

	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_command(db[0], "SET %s %s", key, val1);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	size_t i=0;
	
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		reply=swarmkv_command(db[i], "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, val1);
		swarmkv_reply_free(reply);
	}

	reply=swarmkv_command(db[0], "SET %s %s", key, val2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	wait_for_sync();
	sleep(2);
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		reply=swarmkv_command_on(db[i], swarmkv_self_address(db[i]), "CRDT EXISTS %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 1);
		swarmkv_reply_free(reply);
	}
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		reply=swarmkv_command(db[i], "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
		EXPECT_STREQ(reply->str, val2);
		swarmkv_reply_free(reply);
	}
	reply=swarmkv_command(db[TEST_NODE_NUMBER-1], "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	wait_for_sync();

	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		reply=swarmkv_command(db[i], "GET %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_NIL);
		swarmkv_reply_free(reply);
	}

}
int main(int argc, char ** argv)
{
	int ret=0;
	g_current_thread_id=syscall(SYS_gettid);
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
