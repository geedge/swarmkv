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

#define	BASIC_TEST_CLUSTER_PORT	5210
#define BASIC_TEST_HEALTH_PORT	6210
#define TWO_NODES_TEST_CLUSTER_PORT	5211
#define TWO_NODES_TEST_HEALTH_PORT	6211
#define N_NODES_TEST_CLUSTER_PORT	5311
#define N_NODES_TEST_HEALTH_PORT	6311

#define	CMD_EXEC_TIMEOUT_MS	1000*2

static int g_current_thread_id=0;
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
		char ip_list[1024]={0};
		snprintf(ip_list, sizeof(ip_list), "127.0.0.1:%d", BASIC_TEST_CLUSTER_PORT);
		swarmkv_cli_create_cluster(cluster_name, ip_list);
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options *opts=swarmkv_options_new();
		swarmkv_options_set_cluster_port(opts, BASIC_TEST_CLUSTER_PORT);
		swarmkv_options_set_health_check_port(opts, BASIC_TEST_HEALTH_PORT);
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
		//swarmkv_command(db, "monreg %s", swarmkv_self_address(db));
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

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "string");
	swarmkv_reply_free(reply);

	const char *string_key="string-key";
	reply=swarmkv_command(db, "SET %s abc", string_key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);


	key="name2";
	val="lisi";
	swarmkv_set(db, key, strlen(key), val, strlen(val), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	swarmkv_get(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
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

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "integer");
	swarmkv_reply_free(reply);

	key="int2";
	swarmkv_incrby(db, key, strlen(key), 100, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 100);
	swarmkv_reply_free(reply);
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

	reply=swarmkv_command(db, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	swarmkv_del(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);	
}
TEST_F(SwarmkvBasicTest, GenericTTL)
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

	//API test
	reply=swarmkv_command(db, "SET %s %s", key, val);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	swarmkv_expire(db, key, strlen(key), seconds, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	swarmkv_ttl(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, seconds);
	swarmkv_reply_free(reply);

	swarmkv_persist(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeGeneric)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	struct swarmkv_reply *reply=NULL;
	
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

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "set");
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

	//API test
	size_t member_len[4]={strlen(member[0]), strlen(member[1]), strlen(member[2]), strlen(member[3])};
	swarmkv_sadd(db, key, strlen(key), member, member_len, 4, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 4);
	swarmkv_reply_free(reply);
	
	swarmkv_srem(db, key, strlen(key), member, member_len, 2, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 2);
	swarmkv_reply_free(reply);

	swarmkv_sismember(db, key, strlen(key), member[0], strlen(member[0]), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	swarmkv_sismember(db, key, strlen(key), member[2], strlen(member[2]), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	swarmkv_smembers(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, 2);
	for(i=0; i<reply->n_element; i++)
	{
		EXPECT_EQ(reply->elements[i]->type, SWARMKV_REPLY_STRING);
	}
	swarmkv_reply_free(reply);

	swarmkv_scard(db, key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 2);
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

	reply=swarmkv_command(db, "type %s", key2);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "hash");
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

	long long period=10;
	reply=swarmkv_command(db, "TCFG %s %lld %lld PD %lld", key, rate, capacity, period);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	allocated_tokens=0;
	start=now;
	while(now.tv_sec - start.tv_sec<period*3)
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
	EXPECT_LE(allocated_tokens, (now.tv_sec - start.tv_sec)*rate/period+capacity);	

	//Infinite tokens
	reply=swarmkv_command(db, "TCFG %s 1 1 PD 0", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	reply=swarmkv_command(db, "TINFO %s", key);
	ASSERT_EQ(reply->n_element, 12);
	allocated_tokens=reply->elements[7]->integer;
	swarmkv_reply_free(reply);
	
	long long inf_token=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "TCONSUME %s 10000", key);
		inf_token+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(inf_token, 10000*i);
	reply=swarmkv_command(db, "TINFO %s", key);
	ASSERT_EQ(reply->n_element, 12);
	EXPECT_EQ(reply->elements[7]->integer, allocated_tokens+inf_token);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "token-bucket");
	swarmkv_reply_free(reply);

	swarmkv_tconsume(db, key, strlen(key), 100, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 100);
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
	reply=swarmkv_command(db, "FTCFG %s %lld %lld 128 PD 0", key, rate, capacity);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	long long inf_token=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "FTCONSUME %s user-001 5 10000", key);
		inf_token+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(inf_token, 10000*i);
	reply=swarmkv_command(db, "FTINFO %s", key);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_EQ(reply->elements[7]->integer, allocated_tokens+inf_token);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "fair-token-bucket");
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
	int round=0, n_member=120;
	while(now.tv_sec - start.tv_sec<3)
	{
		request_tokens=random()%(2*rate);
		reply=swarmkv_command(db, "BTCONSUME %s user-%d %lld", key, round%n_member, request_tokens);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			allocated_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		round++;
	}
	printf("consume round %d, speed %d ops\n", round, round/(int)(now.tv_sec-start.tv_sec));
	EXPECT_LE(allocated_tokens/n_member, (now.tv_sec -start.tv_sec)*rate+capacity);	
	reply=swarmkv_command(db, "BTINFO %s", key);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_NEAR(reply->elements[9]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);

	//Infinite tokens
	reply=swarmkv_command(db, "BTCFG %s 0 0 256 PD 0", key);
	EXPECT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	long long t=0;
	int i=0;
	for(i=0; i<100; i++)
	{
		reply=swarmkv_command(db, "BTCONSUME %s user-001 10000", key);
		t+=reply->integer;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(t, 10000*i);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "bulk-token-bucket");
	swarmkv_reply_free(reply);
}

TEST_F(SwarmkvBasicTest, TypeBloomFilter)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="bf-001";
	const char *item[4]={"zhangsan", "lisi", "王二麻子", "Tom"};
	struct swarmkv_reply *reply=NULL;
	long long time_window_ms=600, capacity=10000;
	double error_rate=0.001;
	reply=swarmkv_command(db, "BFINIT %s %f %lld TIME %lld 12", key, error_rate, capacity, time_window_ms);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	
	reply=swarmkv_command(db, "BFADD %s %s %s %s %s", key, item[0], item[1], item[2], item[3]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFEXISTS %s %s", key, item[0]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFMEXISTS %s %s %s %s %s", key, item[0], item[1], item[2], item[3]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 4);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFMEXISTS %s %s %s %s %s %s", key, item[0], item[1], item[2], item[3], "non-exist-item");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 5);
	for(size_t i=0; i<reply->n_element-1; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	ASSERT_EQ(reply->elements[4]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[4]->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFCARD %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_NEAR(reply->integer, 4, 2);
	swarmkv_reply_free(reply);

	usleep(time_window_ms*1000);
	reply=swarmkv_command(db, "BFMEXISTS %s %s %s %s %s %s", key, item[0], item[1], item[2], item[3], "non-exist-item");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 5);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 0);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 20);
	ASSERT_EQ(reply->elements[1]->type, SWARMKV_REPLY_DOUBLE);
	EXPECT_EQ(reply->elements[1]->dval, error_rate);
	ASSERT_EQ(reply->elements[3]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[3]->integer, capacity);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFMEXISTS %s %s", key, item[0]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFEXISTS %s %s", key, item[0]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	//No time window
	reply=swarmkv_command(db, "BFINIT %s %f %lld", key, error_rate, capacity);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	
	reply=swarmkv_command(db, "BFADD %s %s %s %s %s", key, item[0], item[1], item[2], item[3]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "BFEXISTS %s %s", key, item[0]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "bloom-filter");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	ASSERT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	//API test
	key="bf-api";
	reply=swarmkv_command(db, "BFINIT %s %f %lld TIME %lld 12", key, error_rate, capacity, time_window_ms);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	size_t item_len[4]={strlen(item[0]), strlen(item[1]), strlen(item[2]), strlen(item[3])};
	swarmkv_bfadd(db, key, strlen(key), item, item_len, 4, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	swarmkv_bfmexists(db, key, strlen(key), item, item_len, 4, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 4);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
}
TEST_F(SwarmkvBasicTest, TypeCMS)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="cms-001";
	struct swarmkv_reply *reply=NULL;
	long long width=100, depth=10;
	reply=swarmkv_command(db, "CMSINITBYDIM %s %lld %lld", key, width, depth);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	int n_item=5;
	const char *item[n_item]={"zhangsan", "lisi", "王二麻子", "Tom", "铁蛋"};
	reply=swarmkv_command(db, "CMSINCRBY %s %s 1 %s 1 %s 1 %s 1 %s 1", key, item[0], item[1], item[2], item[3], item[4]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, n_item);
	for(int i=0; i<n_item; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "CMSMQUERY %s %s %s %s %s %s", key, item[0], item[1], item[2], item[3], item[4]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, n_item);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "CMSQUERY %s %s", key, "non-exist-item");
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "CMSINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 12);
	ASSERT_EQ(reply->elements[1]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[1]->integer, width);
	ASSERT_EQ(reply->elements[3]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[3]->integer, depth);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "count-min-sketch");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	//API test
	key="cms-api";
	size_t item_len[5]={strlen(item[0]), strlen(item[1]), strlen(item[2]), strlen(item[3]), strlen(item[4])};
	long long count[5]={1, 2, 3, 4, 5};
	reply=swarmkv_command(db, "CMSINITBYDIM %s %lld %lld", key, width, depth);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	swarmkv_cmsincrby(db, key, strlen(key), item, item_len, count, 5, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 5);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, count[i]);
	}
	swarmkv_reply_free(reply);

	swarmkv_cmsmquery(db, key, strlen(key), item, item_len, 5, copy_reply_callback, &reply);
	swarmkv_caller_loop(db, SWARMKV_LOOP_ONCE, NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 5);
	for(size_t i=0; i<reply->n_element; i++)
	{
		ASSERT_EQ(reply->elements[i]->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->elements[i]->integer, count[i]);
	}
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvBasicTest, TypeHyperloglog)
{
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="hll-001";
	int n_item=1000;
	struct swarmkv_reply *reply=NULL;
	char precision=9;
	reply=swarmkv_command(db, "PFINIT %s %d", key, precision);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
	
	reply=swarmkv_command(db, "PFADD %s item-%d", key, 0);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);


	for(int i=0; i<n_item; i++)
	{
		reply=swarmkv_command(db, "PFADD %s %d", key, i);
		swarmkv_reply_free(reply);
	}
	reply=swarmkv_command(db, "PFCOUNT %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_NEAR(reply->integer, n_item+1, n_item/10);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "PFINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 6);
	ASSERT_EQ(reply->elements[3]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[3]->integer, precision);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "type %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "hyperloglog");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	//Time Hyperloglog
	long long time_window_ms=1000;
	reply=swarmkv_command(db, "PFINIT %s %d TIME %lld", key, precision, time_window_ms);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	struct timeval pfadd_ts[n_item];
	for(int i=0; i<n_item; i++)
	{
		gettimeofday(pfadd_ts+i, NULL);
		reply=swarmkv_command(db, "PFADD %s %d", key, i);
		swarmkv_reply_free(reply);
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	reply=swarmkv_command(db, "PFCOUNT %s", key);
	long long in_window_count=0;
	for(int i=0; i<n_item; i++)
	{
		if(timeval_delta_ms(pfadd_ts[i], now)<=time_window_ms)
		{
			in_window_count++;
		}
		
	}
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_NEAR(reply->integer, in_window_count, in_window_count/10);
	swarmkv_reply_free(reply);

	//The STHLL reset counter every 2W/m interval, so it takes 2W time to reset all counters.
	usleep(time_window_ms*1000*2);
	reply=swarmkv_command(db, "PFCOUNT %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db, "PFINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 6);
	ASSERT_EQ(reply->elements[5]->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->elements[5]->integer, time_window_ms);
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

		char ip_list[1024]={0};
		snprintf(ip_list, sizeof(ip_list), "127.0.0.1:%d 127.0.0.1:%d", TWO_NODES_TEST_CLUSTER_PORT, TWO_NODES_TEST_CLUSTER_PORT+1);
		swarmkv_cli_create_cluster(cluster_name, ip_list);
		unsigned int very_long_timeout_us=600*1000*1000;
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options *opts[2];
		struct swarmkv *tmp_db[2];
		for(int i=0; i<2; i++)
		{
			opts[i]=swarmkv_options_new();
			swarmkv_options_set_cluster_port(opts[i], TWO_NODES_TEST_CLUSTER_PORT+i);
			swarmkv_options_set_health_check_port(opts[i], TWO_NODES_TEST_HEALTH_PORT+i);
			swarmkv_options_set_logger(opts[i], logger);
			swarmkv_options_set_sync_interval_us(opts[i], 10000);
			swarmkv_options_set_cluster_timeout_us(opts[i], very_long_timeout_us);
			swarmkv_options_set_worker_thread_number(opts[i], 2);
			swarmkv_options_set_caller_thread_number(opts[i], 1);
			swarmkv_options_set_batch_sync_enabled(opts[i], 1);
			tmp_db[i]=swarmkv_open(opts[i], cluster_name, &err);
			if(err)
			{
				printf("swarmkv_open failed: %s\n", err);
				free(err);
				err=NULL;
			}
			swarmkv_register_thread(tmp_db[i]);
		}
		db1=tmp_db[0];
		db2=tmp_db[1];
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
TEST_F(SwarmkvTwoNodes, KeyspaceKeys)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *key="keyspace-230813";

	reply=swarmkv_command(db[0], "SET %s value1", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "KEYSPACE RLIST %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, 1);
	swarmkv_reply_free(reply);

	int nr_worker_thread=0;
	const struct swarmkv_options *opts=NULL;
	opts=swarmkv_get0_options(db[0]);
	nr_worker_thread=swarmkv_options_get_worker_thread_number(opts);
	long long n_key=0;
	for(int i=0; i<nr_worker_thread; i++)
	{
		reply=swarmkv_command(db[1], "KEYSPACE KEYS %d %s", i, key);
		if(reply->type==SWARMKV_REPLY_ARRAY) n_key+=reply->n_element;
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(n_key, 1);
}
TEST_F(SwarmkvTwoNodes, CRDT)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *key="low-level-test-230813";

	reply=swarmkv_command(db[0], "SET %s value1", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STRING);
	EXPECT_STREQ(reply->str, "value1");
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "CRDT INFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	EXPECT_EQ(reply->n_element, 8);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "CRDT GET %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_VERBATIM);
	swarmkv_reply_free(reply);

}
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
	int i=0, round=1*1000;
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
	swarmkv_caller_loop(db[0], SWARMKV_LOOP_ONCE, NULL);
	ASSERT_TRUE(reply!=NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, n_member/2);
	swarmkv_reply_free(reply);
	reply=NULL;

	swarmkv_sadd(db[1], key, strlen(key), (const char**)  members+n_member/2, member_len+n_member/2, n_member-n_member/2, copy_reply_callback, &reply);	
	swarmkv_caller_loop(db[1], SWARMKV_LOOP_ONCE, NULL);
	ASSERT_TRUE(reply!=NULL);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, n_member-n_member/2);
	swarmkv_reply_free(reply);
	reply=NULL;
	
	wait_for_sync();

	swarmkv_smembers(db[1], key, strlen(key), copy_reply_callback, &reply);
	swarmkv_caller_loop(db[1], SWARMKV_LOOP_ONCE, NULL);
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
			swarmkv_caller_loop(db[i%2], SWARMKV_LOOP_ONCE, NULL);
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
			swarmkv_caller_loop(db[(i+1)%2], SWARMKV_LOOP_ONCE, NULL);
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

	long long capacity=512*1024, rate=512*1024;

	reply=swarmkv_command(db[0], "TCFG %s %lld %lld", key, rate, capacity);	
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	reply=NULL;


	srandom(171);
	
	int i=0, run_second=10;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, allocated_tokens=0;
	//Two heavy consumers
	long long elapsed_ms=0;
	while(elapsed_ms/1000<run_second)
	{
		token=random()%(4*rate);
		requested_tokens+=token;
		reply=swarmkv_command(db[i%2], "TCONSUME %s %lld FLEXIBLE", key, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		elapsed_ms=(now.tv_sec-start.tv_sec)*1000+(now.tv_usec-start.tv_usec)/1000;
		i++;
	}

	EXPECT_GE(i/(int)(now.tv_sec-start.tv_sec), 10000);//>10,000 QPS
	long long upper_limit=elapsed_ms*rate/1000+capacity;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	//waste no more than (n_replica-1)/n_replica of tokens
	EXPECT_NEAR(accuracy, 1, (double)0.5/run_second);
	printf("requested %lld, upper limit %lld, allocated %lld\n", requested_tokens, upper_limit, allocated_tokens);
	printf("waste %lld, round %d, speed %d qps\n", upper_limit-allocated_tokens, i, i/(int)(now.tv_sec-start.tv_sec));

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
struct ftb_class
{
	long long id;
	long long weight;
	long long requested_tokens;
	long long got_tokens;
	long long requested_round;
	struct timeval last_request;
};
TEST_F(SwarmkvTwoNodes, TypeFairTokenBucket)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	
	const char *key="shaping-profile-with-fairness";
	long long capacity=1*1024*1024, rate=20*1024*1024, divisor=1024;
	
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
	size_t n_member=5;
	struct ftb_class mb[n_member]={{.id=1, .weight=1}, 
								{.id=2, .weight=2},
								{.id=3, .weight=3},
								{.id=4, .weight=4},
								{.id=5, .weight=5}};
	long long token=0, requested_tokens=0, allocated_tokens=0;
	long long elapsed_ms=0;
	long long run_second=20;
	long long req_interval_us=0;
	while(elapsed_ms/1000<run_second)
	{
		int idx=round%n_member;
		gettimeofday(&now, NULL);
		if(mb[idx].last_request.tv_sec==0)
		{
			mb[idx].last_request=now;
		}
		req_interval_us=timeval_delta_us(mb[idx].last_request, now);
		if(req_interval_us==0) continue;
		token=rate*req_interval_us/1000000/n_member;
		token+=random()%token;
		mb[idx].last_request=now;
		requested_tokens+=token;
		mb[idx].requested_tokens+=token;
		reply=swarmkv_command(db[idx%2], "FTCONSUME %s user-%lld %lld %lld",
							key, 
							mb[idx].id, mb[idx].weight, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		assert(reply->integer>=0);
		mb[idx].got_tokens+=reply->integer;
		mb[idx].requested_round++;
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);

		
		elapsed_ms=(now.tv_sec-start.tv_sec)*1000+(now.tv_usec-start.tv_usec)/1000;
		round++;
	}
//	printf("consume round %d, speed %d ops\n", i, i/(int)(now.tv_sec-start.tv_sec));
	
	EXPECT_GE(round/(int)(now.tv_sec-start.tv_sec), 10000);
	long long upper_limit=elapsed_ms*rate/1000+capacity;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.05);
#ifdef DEBUG
	printf("requested %lld, upper limit %lld, allocated %lld\n", requested_tokens, upper_limit, allocated_tokens);
#endif
	long long normalized_tokens[n_member]={0};
	for(size_t i=0; i<n_member; i++)
	{
		normalized_tokens[i]=mb[i].got_tokens/mb[i].weight;
#ifdef DEBUG
		printf("member %lld, weight %lld, requested %lld, got %lld\n", 
				mb[i].id, mb[i].weight, mb[i].requested_tokens, mb[i].got_tokens);
#endif
	}
	double deviation=stddev(normalized_tokens, n_member);
	EXPECT_NEAR(deviation/mb[0].got_tokens, 0, 0.1);

	//wait_for_sync();

	reply=swarmkv_command(db[0], "FTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_NEAR(reply->elements[15]->integer, n_member, (double)n_member/5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "FTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_NEAR(reply->elements[15]->integer, n_member, (double)n_member/5);
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
	long long period=5, capacity=15*1024*1024, rate=10*1024*1024*period, buckets=8192;
	struct swarmkv_reply *reply=NULL;
	
	reply=swarmkv_command(db[0], "BTCFG %s %lld %lld %lld PD %lld", key, rate, capacity, buckets, period);
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

	while(now.tv_sec - start.tv_sec<30)
	{
		token=random()%(2*rate/n_member/period);
		requested_tokens+=token;
		
		reply=swarmkv_command(db[member_id%2], "BTCONSUME %s user-%lld %lld", key, member_id, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);

		gettimeofday(&now, NULL);
		member_id=(member_id+1)%n_member;
		round++;
	}
	printf("consume round %d, speed %d ops\n", round, round/(int)(now.tv_sec-start.tv_sec));
	//EXPECT_GE(round/(int)(now.tv_sec-start.tv_sec), 5000);
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate/period+capacity;
	upper_limit=upper_limit*n_member;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);
	wait_for_sync();
	reply=swarmkv_command(db[0], "BTINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_NEAR(reply->elements[9]->integer, n_member, n_member/5);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "BTINFO %s user-001", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 16);
	EXPECT_NEAR(reply->elements[9]->integer, n_member, n_member/5);
	EXPECT_LE(reply->elements[11]->integer, capacity);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "DEL %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "KEYSPACE EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "KEYSPACE EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	wait_for_sync();

	reply=swarmkv_command(db[0], "CRDT EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "CRDT EXISTS %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1],"BTCONSUME %s user-001 1000", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, -1);
	swarmkv_reply_free(reply);

}
TEST_F(SwarmkvTwoNodes, TypeBloomFilter)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *key="bloom-filter-001";
	const char *item[4]={"hello", "world", "bloom", "filter"};
	reply=swarmkv_command(db[0], "BFINIT %s 0.0001 1000000 time 3000000 13", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);

	for(int i=0; i<3; i++)
	{
		reply=swarmkv_command(db[i%2], "BFADD %s %s", key, item[i]);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		swarmkv_reply_free(reply);
	}
	reply=swarmkv_command(db[1], "BFMEXISTS %s %s %s %s", key, item[0], item[1], item[2]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 3);
	for(int i=0; i<3; i++)
	{
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "BFMEXISTS %s %s %s %s", key, item[0], item[1], item[2]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 3);
	for(int i=0; i<3; i++)
	{
		EXPECT_EQ(reply->elements[i]->integer, 1);
	}
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "BFEXISTS %s none-exists", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 0);
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[0], "BFADD %s %s", key, item[3]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);

	usleep(100*1000);

	reply=swarmkv_command(db[1], "BFEXISTS %s %s", key, item[3]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
	EXPECT_EQ(reply->integer, 1);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, TypeCMS)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *key="count-min-sketch-001";
	int n_item=3;
	const char *item[n_item]={"count", "min", "sketch"};
	int count[n_item]={0};

	double error=0.002, probability=0.01;
	reply=swarmkv_command(db[0], "CMSINITBYPROB %s %f %f", key, error, probability);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	srand(171);
	int round=100;
	for(int i=0; i<round; i++)
	{
		int id=i%n_item;
		int increment=random()%100;
		count[id]+=increment;
		reply=swarmkv_command(db[i%2], "CMSINCRBY %s %s %d", key, item[id], increment);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
		ASSERT_EQ(reply->n_element, 1);
		swarmkv_reply_free(reply);
		reply=swarmkv_command(db[(i+1)%2], "CMSQUERY %s %s", key, item[id]);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		swarmkv_reply_free(reply);
	}
	wait_for_sync();
	reply=swarmkv_command(db[1], "CMSINFO %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	long long total_count=reply->elements[9]->integer;
	swarmkv_reply_free(reply);

	reply=swarmkv_command(db[1], "CMSMQUERY %s %s %s %s", key, item[0], item[1], item[2]);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, n_item);
	for(int i=0; i<n_item; i++)
	{
		EXPECT_NEAR(reply->elements[i]->integer, count[i], total_count*error);
	}
	swarmkv_reply_free(reply);
	
	reply=swarmkv_command(db[0], "CMSRLIST %s", key);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_ARRAY);
	ASSERT_EQ(reply->n_element, 2);
	swarmkv_reply_free(reply);

	char uuid[37];
	swarmkv_self_uuid(db[0], uuid);
	reply=swarmkv_command(db[0], "CMSRCLEAR %s %s", key, uuid);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	swarmkv_reply_free(reply);
}
TEST_F(SwarmkvTwoNodes, TypeHyperLogLog)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	const char *key="hll-001";
	const char *prefix[4]={"Philippe", "Flajolet", "Invents", "Hyperloglog"};


	char precision=12;
	reply=swarmkv_command(db[0], "PFINIT %s %d", key, precision);
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	srand(171);
	int n_item=10000;
	for(int i=0; i<n_item; i++)
	{
		reply=swarmkv_command(db[i%2], "PFADD %s %s-%d", key, prefix[i%4], i);
		swarmkv_reply_free(reply);
		if(i%1000==0)
		{
			wait_for_sync();
			reply=swarmkv_command(db[(i+1)%2], "PFCOUNT %s", key);
			ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
			EXPECT_NEAR(reply->integer, i+1, i/20);
			swarmkv_reply_free(reply);
		}
	}
}
TEST_F(SwarmkvTwoNodes, Info)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<2; i++)
	{
		reply=swarmkv_command(db[i%2], "INFO");
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		swarmkv_reply_free(reply);
	}
}
TEST_F(SwarmkvTwoNodes, Monitor)
{
	struct swarmkv *db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<2; i++)
	{
		reply=swarmkv_command(db[i%2], "MONREG %s g?t", swarmkv_self_address(db[i%2]));
		ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
		swarmkv_reply_free(reply);
	}
}
TEST_F(SwarmkvTwoNodes, Wait)
{
	//return;
	sleep(3600*2);
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
#define TEST_NODE_NUMBER	8
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
			snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", N_NODES_TEST_CLUSTER_PORT+i);
		}
		const char *cluster_name="swarmkv-n-nodes";
		swarmkv_cli_create_cluster(cluster_name, node_list_str);
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options *opts[TEST_NODE_NUMBER];
		for(i=0; i<TEST_NODE_NUMBER; i++)
		{
			opts[i]=swarmkv_options_new();			
			swarmkv_options_set_cluster_port(opts[i], N_NODES_TEST_CLUSTER_PORT+i);
			swarmkv_options_set_health_check_port(opts[i], N_NODES_TEST_HEALTH_PORT+i);
			swarmkv_options_set_worker_thread_number(opts[i], 2);
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
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		reply=swarmkv_command(db[i], "CRDT EXISTS %s", key);
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
		reply=swarmkv_command(db[i], "CRDT EXISTS %s", key);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		EXPECT_EQ(reply->integer, 0);
		swarmkv_reply_free(reply);
	}

}
TEST_F(SwarmkvNNodes, TypeTokenBucket)
{
	struct swarmkv **db=SwarmkvNNodes::db;
	
	const char *key="tb";

	struct swarmkv_reply *reply=NULL;

	long long capacity=512*1024, rate=512*1024;

	reply=swarmkv_command(db[0], "TCFG %s %lld %lld", key, rate, capacity);	
	ASSERT_EQ(reply->type, SWARMKV_REPLY_STATUS);
	EXPECT_STREQ(reply->str, "OK");
	swarmkv_reply_free(reply);
	reply=NULL;


	srandom(171);
	
	int i=0, run_second=100;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, allocated_tokens=0;
	//Two heavy consumers
	while(now.tv_sec - start.tv_sec<run_second)
	{
		token=random()%(4*rate);
		requested_tokens+=token;
		reply=swarmkv_command(db[i%TEST_NODE_NUMBER], "TCONSUME %s %lld FLEXIBLE", key, token);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		allocated_tokens+=reply->integer;
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	
	EXPECT_GE(i/(int)(now.tv_sec-start.tv_sec), 10000);//>10,000 QPS
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate+capacity;
	double accuracy=(double)allocated_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);
	printf("requested %lld, upper limit %lld, allocated %lld\n", requested_tokens, upper_limit, allocated_tokens);
	printf("waste %lld, round %d, speed %d qps\n", upper_limit-allocated_tokens, i, i/(int)(now.tv_sec-start.tv_sec));

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
int main(int argc, char ** argv)
{
	int ret=0;
	g_current_thread_id=syscall(SYS_gettid);
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
