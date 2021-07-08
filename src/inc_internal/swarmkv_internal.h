#pragma once
#include<stdio.h>
#include <stdint.h>
#include "./uthash/uthash.h"
#include "./uthash/utarray.h"

#define SWARMKV_DEFAULT_SLOT_NUM 65535
#define BOOTSTRAPS_NUM 5

struct swarmkv_key_value
{
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
	UT_array *nodes_have_key_cached; //for key invalidation
	UT_hash_handle hh;
};

struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
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

/*
struct swarmkv_transaction
{
	char uuid[32];
	swarmkv_callback_func_t callback;
	void* usrdata;
	UT_hash_handle hh;
};
*/

struct swarmkv_node
{
	int node_id;       //Key
	char* ip;
	uint16_t port;
	//int virtual_num;
	uint32_t ping_sent_t;
	uint32_t pong_received_t;
	int status;        //ONLINE/PFAIL/FAIL
	int pfail_cnt;
	UT_hash_handle hh;
};

/*
struct swarmkv_ch_node
{
	uint32_t	node_id;       
	uint32_t	ch_hash_val;     
};
*/

struct swarmkv_store
{
	int my_node_id; 
	char* my_ip;
	uint16_t my_port;  
	char uuid[32];
	struct swarmkv_slot slots[SWARMKV_DEFAULT_SLOT_NUM];
	struct swarmkv_table *table_hash; //key: char table_name[256], value: struct swarmkv_table*
	struct swarmkv_node *nodes;		  //key: int node_id, value: struct swarmkv_node*
	int sockfd;
	//struct swarmkv_transaction *on_fly_transactions;
	pthread_rwlock_t rwlock;
	//struct swarmkv_ch_node* ch_nodes;
};

/*
struct swarmkv_readoptions{};
struct swarmkv_writeoptions{};
*/