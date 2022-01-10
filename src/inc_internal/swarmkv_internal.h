#pragma once
#include <stdio.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include "./uthash/uthash.h"
#include "./uthash/utarray.h"
#include "swarmkv_utils.h"
#include "swarmkv_net.h"
#include "swarmkv.h"
#include "swarmkv_nng.h"
#include<time.h>


#define SWARMKV_DEFAULT_NODE_TIMEOUT_US 15000000    //15s
#define SWARMKV_DEFAULT_PING_MIN_INTERVAL_US 3000000    //5s
#define SWARMKV_REGULAR_PING_INTERVAL_US 1000000   //1s
#define SWARMKV_RANDOM_PING_INTERVAL_US 1000000   //1s
#define SWARMKV_CACHE_EXPIREATION_PERIORD  60000000  //1min

//#define SWARMKV_DEFAULT_SLOT_NUM 65536    //redis 16284
#define SWARMKV_DEFAULT_SLOT_NUM 1024
#define SWARMKV_DEFAULT_NODE_NUM 256
#define SWARMKV_POINTS_PER_NODE_ON_CHR 3
#define BOOTSTRAPS_NUM 5


#define FAILED 0
#define PFAIL 1
#define ON 2
#define WAITMEET 3
#define WAITLEAVE 4

#define CLUSTER_STABLE 0
#define CLUSTER_STRETCH 1

#define SLOT_STABLE 0
#define SLOT_MIGRATING 1

#define TOKEN_LEN 36
#define BUFSIZE 65507

#define SWARMKV_DEFAULT_LISTEN_PORT 8323
#define SWARMKV_MAX_KEY_LEN 64
#define SWARMKV_MAX_VAL_LEN 1024
#define SWARMKV_ONCE_MIGRATE_KV_NUM 40
#define SWARMKV_REDUNDANCY_NUM 3

struct swarmkv_key_value
{
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
	clock_t cached_time;
	UT_array *nodes_have_key_cached; //for key invalidation
	UT_hash_handle hh;
};

struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
	int replica_node_id[SWARMKV_REDUNDANCY_NUM];
	int status;                 //0 normal/1 migrating
	pthread_rwlock_t rwlock;
	struct swarmkv_table *table_hash;
};

struct swarmkv_table
{
	char table_name[256];		 //Key
	//int update_interval;   //bin log?
	int cache_invalidation_mode; //Tracking or broadcasting
	struct swarmkv_key_value *owned_value_hash;
	struct swarmkv_key_value *cached_value_hash;
	pthread_rwlock_t rwlock;
	UT_hash_handle hh;
};

struct swarmkv_node
{
	int node_id;       //Key
	char* ip;
	uint16_t port;
	//int virtual_num;
	uint32_t ping_sent_time;
	uint32_t pong_received_time;
	int status;        //ONLINE/PFAIL/FAIL
	int pfail_cnt;
	UT_hash_handle hh;
};

struct swarmkv_store
{
	int my_node_id; 
	char* my_ip;
	uint16_t my_port;  
	//uuid_t uuid;
	char uuid[37];
	int cluster_size;
	int cluster_state;     //0 STABLE || 1 MEETING
	int leave_recieve_cnt;
	//uint32_t meet_time;    //meet不能超时
	int meet_flag;
	struct swarmkv_node *nodes;		  //key: int node_id, value: struct swarmkv_node*
	int sockfd;
	int nng_socket;
	struct swarmkv_slot slots[SWARMKV_DEFAULT_SLOT_NUM];
	pthread_t tid;
	//struct swarmkv_table *table_hash; //key: char table_name[256], value: struct swarmkv_table*
	//struct swarmkv_transaction *on_fly_transactions;
	pthread_rwlock_t rwlock;
};

struct config_information
{
	int node_id;
	char token[TOKEN_LEN];
	int db_id;
};


struct swarmkv_readoptions{};
struct swarmkv_writeoptions{};

struct msg_count
{
	int total_cnt;
	int ping_cnt;
	int pong_cnt;
	int meet_req_cnt;
	int meet_resp_cnt;
	int fail_req_cnt;
	int fail_resp_cnt;
	int migrate_req_cnt;
	int migrate_resp_cnt;
	int del_kv_cnt;
	int slot_moved_cnt;
	int put_req_cnt;
	int put_resp_cnt;
	int get_req_cnt;
	int get_resp_cnt;
	int invalidate_req_cnt;
	int invalidate_resp_cnt;
	int join_complete_cnt;
};

