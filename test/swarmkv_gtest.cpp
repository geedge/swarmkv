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

void generic_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
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

class SwarmkvBasicTest : public testing::Test 
{

protected:	
	static void SetUpTestCase()
	{
		const char* log_path="./basic-test.log";
		char *err=NULL;
		const char *cluster_name="swarmkv-basic-test";
		swarmkv_cli_create_cluster(cluster_name, "127.0.0.1:5210");
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts, 5210);
		swarmkv_options_set_health_check_port(opts, 6210);
		swarmkv_options_set_logger(opts, logger);
		db=swarmkv_open(opts, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s.\n", err);
			free(err);
		}
	}
	static void TearDownTestCase()
	{
		swarmkv_close(db);		
		log_handle_destroy(logger);
	}	
 	// Some expensive resource shared by all tests.
 	
	static struct log_handle *logger;
	static struct swarmkv* db;
};
struct swarmkv *SwarmkvBasicTest::db;
struct log_handle *SwarmkvBasicTest::logger;
TEST_F(SwarmkvBasicTest, TypeString)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_cstring(arg, val);
	swarmkv_get(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, TypeInteger)
{
	struct swarmkv* db=SwarmkvBasicTest::db;
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
TEST_F(SwarmkvBasicTest, DEL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="name2";
	const char* val="zhangsan";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, TypeSet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="Jack's friends";
	const char* member[]={"zhangsan", "lisi", "王二麻子", "Tom", "مرحبا"};
	size_t member_len[32];
	size_t n_member=0, i=0;
	n_member=sizeof(member)/sizeof(const char*);
	for(i=0; i<n_member; i++)
	{
		member_len[i]=strlen(member[i])+1;
	}

	int exec_successful=0;

	arg=cmd_exec_arg_new();

	//SADD key m0 m2 ... m4
	cmd_exec_arg_expect_integer(arg, n_member);
	swarmkv_sadd(db, key, strlen(key)+1, member, member_len, n_member, generic_callback, arg);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//SMEMEBERS key
	cmd_exec_arg_expect_array(arg, n_member);
	swarmkv_smembers(db, key, strlen(key)+1, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//SREM key m0 m1
	cmd_exec_arg_expect_integer(arg, 2);
	swarmkv_srem(db, key, strlen(key)+1, member, member_len, 2, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//SISMEMBER key m0
	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_sismember(db, key, strlen(key)+1, member[0], member_len[0], generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//SISMEMBER key m2
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_sismember(db, key, strlen(key)+1, member[2], member_len[2], generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//SCARD key
	cmd_exec_arg_expect_integer(arg, 3);
	swarmkv_scard(db, key, strlen(key)+1, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//DEL key
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db, key, strlen(key)+1, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//SISMEMBER key m2
	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_sismember(db, key, strlen(key)+1, member[2], member_len[2], generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//SCARD key
	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_scard(db, key, strlen(key)+1, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	
	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvBasicTest, TypeTokenBucket)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv *db=SwarmkvBasicTest::db;
	const char *key="tb-192.168.0.1";
	int exec_successful=0;
	arg=cmd_exec_arg_new();
	long long capacity=1024*4, rate=1024*2, request_tokens=0, got_tokens=0;
	cmd_exec_arg_expect_OK(arg);	
	swarmkv_command_async(db, generic_callback, arg, NULL, "TCFG %s %lld %lld", key, capacity, rate);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	cmd_exec_arg_clear(arg);
	EXPECT_TRUE(exec_successful);
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	srand(171);
	struct swarmkv_reply *reply=NULL;
	int i=0;
	while(now.tv_sec - start.tv_sec<10)
	{
		request_tokens=random()%(2*rate);
		cmd_exec_arg_expect_integer(arg, 0);
		reply=swarmkv_command(db, "TCONSUME %s %lld FLEXIBLE", key, request_tokens);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			got_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	EXPECT_LE(got_tokens, (now.tv_sec -start.tv_sec)*rate+capacity);	
	cmd_exec_arg_free(arg);
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
}
TEST_F(SwarmkvBasicTest, EXPIRE_TTL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db=SwarmkvBasicTest::db;
	const char* key="quarantine";
	const char* val="wuhan-江夏-如家";
	int exec_successful=0;
	int seconds=3;
	
	//SET key value
	arg=cmd_exec_arg_new();
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	sleep(seconds+2);

	//GET key
	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_expire(db, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//TTL key
	cmd_exec_arg_expect_integer(arg, -2);
	swarmkv_ttl(db, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);
	arg=NULL;
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
		swarmkv_options_set_p2p_port(opts1, 5210);
		swarmkv_options_set_health_check_port(opts1, 6210);
		swarmkv_options_set_logger(opts1, logger);
		db1=swarmkv_open(opts1, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}

		struct swarmkv_options* opts2=swarmkv_options_new();
		swarmkv_options_set_p2p_port(opts2, 5211);
		swarmkv_options_set_health_check_port(opts2, 6211);
		swarmkv_options_set_logger(opts2, logger);
		db2=swarmkv_open(opts2, cluster_name, &err);
		if(err)
		{
			printf("swarmkv_open failed: %s\n", err);
			free(err);
			err=NULL;
		}

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
struct log_handle* SwarmkvTwoNodes::logger;
struct swarmkv* SwarmkvTwoNodes::db1;
struct swarmkv* SwarmkvTwoNodes::db2;
TEST_F(SwarmkvTwoNodes, SET_GET)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	int exec_successful=0;

	arg=cmd_exec_arg_new();
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_cstring(arg, val1);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db2, key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	wait_for_sync();
	
	cmd_exec_arg_expect_cstring(arg, val2);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvTwoNodes, SET1kString)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	const char *key_prefix="test-1k-string";
	const char *val_prefix="value-xx";
	char key[128]="", val[128]="";
	int exec_successful=0, i=0, round=1000, n_success=0;

	arg=cmd_exec_arg_new();	
	cmd_exec_arg_expect_OK(arg);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		snprintf(val, sizeof(val), "%s-%d", val_prefix, i);
		tmp_db=i%2?db1:db2;
		swarmkv_set(tmp_db, key, strlen(key), val, strlen(val), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		snprintf(val, sizeof(val), "%s-%d", val_prefix, i);
		cmd_exec_arg_expect_cstring(arg, val);

		tmp_db=i%2?db2:db1;
		swarmkv_get(tmp_db, key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);

	cmd_exec_arg_free(arg);
}
TEST_F(SwarmkvTwoNodes, INCRYBY1kInteger)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	const char *key_prefix="incrby-1k-integer";
	char key[128]="", val[128]="";
	int exec_successful=0, i=0, round=1000, n_success=0;
	
	arg=cmd_exec_arg_new();	

	n_success=0;
	int increment=-3000000;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		cmd_exec_arg_expect_integer(arg, i+increment);

		tmp_db=i%2?db2:db1;
		swarmkv_incrby(tmp_db, key, strlen(key), i+increment, generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);

	cmd_exec_arg_expect_OK(arg);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);
		snprintf(val, sizeof(val), "0");
		tmp_db=i%2?db1:db2;
		swarmkv_set(tmp_db, key, strlen(key), val, strlen(val), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);

	cmd_exec_arg_expect_cstring(arg, "0");
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "%s-%d", key_prefix, i);		
		tmp_db=i%2?db2:db1;
		swarmkv_get(tmp_db, key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);


	cmd_exec_arg_free(arg);
}

TEST_F(SwarmkvTwoNodes, DEL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id002";
	const char* val="to-be-deleted";	
	int exec_successful=0;
	arg=cmd_exec_arg_new();
	
	//db1: SET key value
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val, strlen(val), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db2: DEL key
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	//db1: GET key
	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db2: GET key
	cmd_exec_arg_expect_NIL(arg);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;

}

TEST_F(SwarmkvTwoNodes, INCRBY)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id003";
	long long val=10000;	
	int exec_successful=0;
	arg=cmd_exec_arg_new();

	//INCRYBY key 10000
	cmd_exec_arg_expect_integer(arg, val);
	swarmkv_incrby(db1, key, strlen(key), val, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//INCRYBY key 100
	cmd_exec_arg_expect_integer(arg, val+100);
	swarmkv_incrby(db2, key, strlen(key), 100, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//INCRYBY key -200	
	cmd_exec_arg_expect_integer(arg, val+100-200);
	swarmkv_incrby(db1, key, strlen(key), -200, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
	arg=NULL;

}
TEST_F(SwarmkvTwoNodes, EXPIRE)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	struct swarmkv* tmp_db=NULL;
	char key[128]="", val[128]="";
	int exec_successful=0, i=0, round=128, n_success=0;
	int max_timeout_seconds=10, min_timeout_seconds=2;
	int seconds=0;
	arg=cmd_exec_arg_new();	

	/*
	 * SET expiring-key-%d value-xx-%d
	*/
	cmd_exec_arg_expect_OK(arg);
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		snprintf(val, sizeof(val), "value-xx-%d", i);
		tmp_db=i%2?db1:db2;
		swarmkv_set(tmp_db, key, strlen(key), val, strlen(val), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);


	/*
	 * EXPIRE expiring-key-%d [min_timeout_seconds, max_timeout_seconds)
	 * Expect integer 1 reply
	*/

	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		cmd_exec_arg_expect_integer(arg, 1);
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		swarmkv_expire(tmp_db, key, strlen(key), seconds, generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);

	sleep(max_timeout_seconds+1);

	/*
	 * GET expiring-key-%d
	 * Expect NIL reply
	*/
	n_success=0;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		cmd_exec_arg_expect_NIL(arg);
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		swarmkv_get(tmp_db, key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful) n_success++;
		cmd_exec_arg_clear(arg);
	}
	EXPECT_EQ(round, n_success);


	/*
	 * KEYSPACE RLIST expiring-key-%d
	 * Expect NIL reply, make sure that expired keys are removed from keyspace.
	*/

	n_success=0;
	struct swarmkv_reply *reply=NULL;
	for(i=0; i<round; i++)
	{ 
		snprintf(key, sizeof(key), "expiring-key-%d", i);
		tmp_db=i%2?db2:db1;
		seconds=min_timeout_seconds+i%(max_timeout_seconds-min_timeout_seconds);
		reply=swarmkv_command_on(tmp_db, NULL, "KEYSPACE RLIST %s", key);
		if(reply->type==SWARMKV_REPLY_NIL)
		{
			n_success++;
		}
		swarmkv_reply_free(reply);
	}
	EXPECT_EQ(round, n_success);

	cmd_exec_arg_free(arg);
	arg=NULL;
}
TEST_F(SwarmkvTwoNodes, TTL)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;

	const char *key="ttl-key-001";
	const char *value="hello-world";
	int exec_successful=0;
	int seconds=3;

	arg=cmd_exec_arg_new();

	// db1: SET key value
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), value, strlen(value), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	// db2: EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db2, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db1: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);

}
TEST_F(SwarmkvTwoNodes, PERSIST)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;

	const char *key="persit-key-001";
	const char *value="hello-world";
	int exec_successful=0;
	int seconds=3;

	arg=cmd_exec_arg_new();

	// db1: SET key value
	
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), value, strlen(value), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	// db1: EXPIRE key seconds
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_expire(db1, key, strlen(key), seconds, generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, seconds);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//db1: PERSIT key
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_persist(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);


	//db2: TTL key
	cmd_exec_arg_expect_integer(arg, -1);
	swarmkv_ttl(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	sleep(seconds+1);

	//db1: GET key
	cmd_exec_arg_expect_cstring(arg, value);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);

}

TEST_F(SwarmkvTwoNodes, FromLocalReplica)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db1=SwarmkvTwoNodes::db1;
	struct swarmkv* db2=SwarmkvTwoNodes::db2;
	const char* key="id004";
	int exec_successful=0;
	arg=cmd_exec_arg_new();



	//db1: SET id003 lisi	
	const char* val1="lisi";

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db1, key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	//db2: GET id003
	cmd_exec_arg_expect_cstring(arg, val1);
	swarmkv_get(db2, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 0);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	//db2: SET id003 wang2mazi	
	const char* val2="wang2mazi";

	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db2, key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 1);
	cmd_exec_arg_clear(arg);

	wait_for_sync();

	//db1: GET id003	
	cmd_exec_arg_expect_cstring(arg, val2);
	swarmkv_get(db1, key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	EXPECT_EQ(arg->is_sync_callback, 1);
	cmd_exec_arg_clear(arg);
	
	cmd_exec_arg_free(arg);

}
TEST_F(SwarmkvTwoNodes, TypeSet)
{
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	const char *key="myset001";
	char **members=NULL;
	size_t *member_len=NULL;
	size_t n_member=128;
	int exec_successful=0;
	arg=cmd_exec_arg_new();

	members=ALLOC(char*, n_member);
	member_len=ALLOC(size_t, n_member);
	for(size_t i=0; i<n_member; i++)
	{
		members[i]=ALLOC(char, 1024);
		member_len[i]=snprintf(members[i], 1024, "set-member-%zu", i);
	}
	
	cmd_exec_arg_expect_integer(arg, n_member/2);
	swarmkv_sadd(db[0], key, strlen(key), (const char**) members, member_len, n_member/2, generic_callback, arg);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_expect_integer(arg, n_member-n_member/2);
	swarmkv_sadd(db[1], key, strlen(key), (const char**)  members+n_member/2, member_len+n_member/2, n_member-n_member/2, generic_callback, arg);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);
	
	wait_for_sync();

	cmd_exec_arg_expect_array(arg, n_member);
	swarmkv_smembers(db[1], key, strlen(key), generic_callback, arg);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	
	for(size_t i=0; i<n_member; i++)
	{
		if(i%3==0)
		{
			cmd_exec_arg_expect_integer(arg, 1);
			swarmkv_srem(db[i%2], key, strlen(key), (const char**) members+i, member_len+i, 1, generic_callback, arg);	
			exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
			cmd_exec_arg_clear(arg);
			EXPECT_TRUE(exec_successful);
		}
	}

	wait_for_sync();

	for(size_t i=0; i<n_member; i++)
	{
		if(i%3==0)
		{
			cmd_exec_arg_expect_integer(arg, 0);
			swarmkv_sismember(db[(i+1)%2], key, strlen(key), members[i], member_len[i], generic_callback, arg);	
			exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
			cmd_exec_arg_clear(arg);
			EXPECT_TRUE(exec_successful);
		}
	}

	//Cleanup
	cmd_exec_arg_free(arg);
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
	struct cmd_exec_arg *arg=NULL;
	struct swarmkv* db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;
	const char *key="tb-192.168.0.1";
	int exec_successful=0;
	arg=cmd_exec_arg_new();
	long long capacity=1024*4, rate=1024*2;
	cmd_exec_arg_expect_OK(arg);	
	swarmkv_command_async(db[0], generic_callback, arg, NULL, "TCFG %s %lld %lld", key, capacity, rate);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	cmd_exec_arg_clear(arg);
	EXPECT_TRUE(exec_successful);
	srandom(171);
	struct swarmkv_reply *reply=NULL;
	int i=0;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);
	long long token=0, requested_tokens=0, got_tokens=0;
	//Two heavy consumers
	while(now.tv_sec - start.tv_sec<5)
	{
		token=random()%(4*rate);
		requested_tokens+=token;
		reply=swarmkv_command_on(db[i%2], NULL, "TCONSUME %s %lld FLEXIBLE", key, token);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			got_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	//Light consumers
	while(now.tv_sec - start.tv_sec<10)
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
		reply=swarmkv_command_on(db[i%2], NULL, "TCONSUME %s %lld FLEXIBLE", key, token);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			got_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
	//One heavy consumer
	while(now.tv_sec - start.tv_sec<15)
	{
		token=rate*3/4+random()%(rate);
		requested_tokens+=token;
		reply=swarmkv_command_on(db[0], NULL, "TCONSUME %s %lld", key, token);
		if(reply->type==SWARMKV_REPLY_INTEGER)
		{
			got_tokens+=reply->integer;
		}
		swarmkv_reply_free(reply);
		gettimeofday(&now, NULL);
		i++;
	}
//	printf("consume round %d, speed %d ops\n", i, i/(int)(now.tv_sec-start.tv_sec));
	EXPECT_GE(i/(int)(now.tv_sec-start.tv_sec), 100000);
	long long upper_limit=(now.tv_sec-start.tv_sec)*rate+capacity;
	double accuracy=(double)got_tokens/(upper_limit<requested_tokens?upper_limit:requested_tokens);
	EXPECT_NEAR(accuracy, 1, 0.035);	

	wait_for_sync();

	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db[0], key, strlen(key), generic_callback, arg);	
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	cmd_exec_arg_clear(arg);
	wait_for_sync();

	cmd_exec_arg_expect_integer(arg, 0);
	swarmkv_command_async(db[1], generic_callback, arg, "TCONSUME %s %lld FLEXIBLE", key, 4096);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	cmd_exec_arg_clear(arg);

	cmd_exec_arg_free(arg);
}
TEST_F(SwarmkvTwoNodes, TypeHash)
{
	struct swarmkv* db[2];
	db[0]=SwarmkvTwoNodes::db1;
	db[1]=SwarmkvTwoNodes::db2;

	size_t n_test_key=100;
	struct swarmkv_reply *reply=NULL;
	for(size_t i=0; i<n_test_key; i++)
	{
		reply=swarmkv_command(db[i%2], "HSET uid-%zu name zhangsan gender male age 18 gender male", i);
		ASSERT_EQ(reply->type, SWARMKV_REPLY_INTEGER);
		ASSERT_EQ(reply->integer, 3);
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
		ASSERT_STREQ(reply->str, "female");
		swarmkv_reply_free(reply);
	}

}
TEST_F(SwarmkvTwoNodes, Wait)
{
	return;
	sleep(3600*2);
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
			snprintf(node_list_str+strlen(node_list_str), sizeof(node_list_str)-strlen(node_list_str), "127.0.0.1:%zu ", 5210+i);
		}
		const char *cluster_name="swarmkv-n-nodes";
		swarmkv_cli_create_cluster(cluster_name, node_list_str);
		logger=log_handle_create(log_path, 0);
		struct swarmkv_options* opts[TEST_NODE_NUMBER];
		for(i=0; i<TEST_NODE_NUMBER; i++)
		{
			opts[i]=swarmkv_options_new();			
			swarmkv_options_set_p2p_port(opts[i], 5210+i);
			swarmkv_options_set_health_check_port(opts[i], 6210+i);
			swarmkv_options_set_worker_thread_number(opts[i], 2);
			swarmkv_options_set_logger(opts[i], logger);
			db[i]=swarmkv_open(opts[i], cluster_name, &err);
			if(err)
			{
				printf("swarmkv_open %zu instance failed: %s\n", i, err);
				free(err);
				err=NULL;
			}
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
	struct cmd_exec_arg *arg=NULL;
	const char *key="id001";
	const char *val1="zhangsan", *val2="lisi";
	int exec_successful=0;
	struct swarmkv **db=SwarmkvNNodes::db;

	arg=cmd_exec_arg_new();
	arg->print_reply_on_fail=1;
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db[0], key, strlen(key), val1, strlen(val1), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	cmd_exec_arg_clear(arg);

	size_t success_cnt=0, i=0;
	
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_cstring(arg, val1);
		swarmkv_get(db[i], key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful)
		{
			success_cnt++;
		}
		
	}
	EXPECT_EQ(success_cnt, TEST_NODE_NUMBER);

	cmd_exec_arg_clear(arg);
	cmd_exec_arg_expect_OK(arg);
	swarmkv_set(db[0], key, strlen(key), val2, strlen(val2), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
	EXPECT_TRUE(exec_successful);
	

	wait_for_sync();

	success_cnt=0;
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_cstring(arg, val2);
		swarmkv_get(db[i], key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful)
		{
			success_cnt++;
		}		
	}
	EXPECT_EQ(success_cnt, TEST_NODE_NUMBER);

	wait_for_sync();

	cmd_exec_arg_clear(arg);
	cmd_exec_arg_expect_integer(arg, 1);
	swarmkv_del(db[TEST_NODE_NUMBER-1], key, strlen(key), generic_callback, arg);
	exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);	
	EXPECT_TRUE(exec_successful);

	wait_for_sync();

	success_cnt=0;
	for(i=0; i<TEST_NODE_NUMBER; i++)
	{
		cmd_exec_arg_clear(arg);
		cmd_exec_arg_expect_NIL(arg);
		swarmkv_get(db[i], key, strlen(key), generic_callback, arg);
		exec_successful=cmd_exec_arg_wait(arg, CMD_EXEC_TIMEOUT_MS);
		if(exec_successful)
		{
			success_cnt++;
		}
		
	}
	EXPECT_EQ(success_cnt, TEST_NODE_NUMBER);
	cmd_exec_arg_free(arg);
	
}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
