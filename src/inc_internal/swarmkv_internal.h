#pragma once
#include <stdio.h>
#include <stdint.h>
#include <uuid/uuid.h>
#include "swarmkv.h"
#include "./uthash/uthash.h"
#include "./uthash/utarray.h"
#include "swarmkv_utils.h"
#include "swarmkv_net.h"

//#define SWARMKV_DEFAULT_SLOT_NUM 65536    //redis 16284
#define SWARMKV_DEFAULT_SLOT_NUM 128
#define SWARMKV_DEFAULT_NODE_NUM 32
#define BOOTSTRAPS_NUM 5

#define PFAIL 0
#define FAIL -1
#define ON 1
#define WAITMEET 2

#define SWARMKV_POINTS_PER_NODE_ON_CHR 3
#define SWARMKV_MAX_MIGRATE_KV_NUM 1000
#define TOKEN_LEN 36

#define BUFSIZE 65500
#define SWARMKV_DEFAULT_LISTEN_PORT 8323
#define SWARMKV_MAX_KEY_LEN 512
#define SWARMKV_MAX_VAL_LEN 2048
#define SWARMKV_ONCE_MIGRATE_KV_NUM 20

struct swarmkv_key_value
{
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
	int migrate_flag;     //0未迁移/1已迁移未应答
	UT_array *nodes_have_key_cached; //for key invalidation
	UT_hash_handle hh;
};

struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
	int last_owner_id;          //迁移之前的上一个持有者
	int status;                 //0 normal/1 migrating
	int replica_node_id[16];
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
	int meet_cnt;
	UT_hash_handle hh;
};

struct swarmkv_store
{
	int my_node_id; 
	char* my_ip;
	uint16_t my_port;  
	//uuid_t uuid;
	char uuid[37];
	//int node_version;     //集群中node信息的版本号，单调递增
	//int slot_version;     //slot的版本号，单调递增，只有版本号比自己大才修改
	struct swarmkv_slot slots[SWARMKV_DEFAULT_SLOT_NUM];
	struct swarmkv_table *table_hash; //key: char table_name[256], value: struct swarmkv_table*
	int cluster_size;
	struct swarmkv_node *nodes;		  //key: int node_id, value: struct swarmkv_node*
	int sockfd;
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
};

