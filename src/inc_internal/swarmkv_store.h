#pragma once

#include "swarmkv_limits.h"
#include "uthash.h"
#include "utarray.h"

#include <pthread.h>
#include <netinet/ip.h>

enum swarmkv_data_type
{
	SWARMKV_DATA_TYPE_STRING,
	SWARMKV_DATA_TYPE_INTEGER,
	SWARMKV_DATA_TYPE_HASH
};
enum swarmkv_command
{
	SWARMKV_CMD_GET,
	SWARMKV_CMD_SET,
	SWARMKV_CMD_DEL
};

enum slot_state
{
	slot_unkonwn,
	slot_stable,
	slot_mirgrating
};

struct kv_pair
{
	enum swarmkv_data_type data_type;
	char* key;
	size_t keylen;
	char* value;
	size_t vallen;
};
struct kv_container
{
	struct kv_pair head;
	UT_array *nodes_have_kv_cached; //used by value owner for cache invalidate notification
	UT_hash_handle hh;
};
struct swarmkv_node
{
	int node_id;
	struct sockaddr_in peer_addr;	//man 7 ip
	char ip_addr[47];
	uint16_t port_nr;	
	struct bufferevent * bev;
};

struct key_route_entry
{
	char* key;
	size_t keylen;
	int owner_node_id;
	UT_hash_handle hh;
};
//slot is an storage concept for partition KV to cluster nodes.
struct key_slot
{
	int slot_id;
	int key_owner_node_id;
//	int replica_node_ids[MAX_REPLICA_NUM];
	enum slot_state state;
};

struct swarmkv_params
{
	char* self_ip_str;
	uint16_t listen_port;
	size_t n_bootstrap_nodes;
	char bootstrap_node_addrs[MAX_BOOTSTRAP_NODES][MAX_IPv6_ADDR_STR_LEN];
	uint16_t boostrap_node_port_nr[MAX_BOOTSTRAP_NODES];	//network order
	int minimum_replica_num;
	int dry_run;
};
struct swarmkv_thread_ctx
{
	pthread_t thr;
	int readable_tid;
	unsigned int thread_id;
	unsigned int load;
	struct swarmkv_store *store;

	struct event_base * evbase;
	time_t lastime;
};

struct swarmkv_store
{
	struct swarmkv_params para;
	unsigned int nr_work_threads;
	struct swarmkv_thread_ctx * work_threads[SWARMKV_THREAD_MAX];

	
	int my_node_id;
	const char* my_addr;
	UT_array* cluster_nodes;
	struct key_route_entry* hash_route_table;
	
	struct kv_container *hash_own_kv;
	struct kv_container *hash_cached_kv;
	
	struct key_slot slots[SLOT_NUM];
	pthread_rwlock_t rwlock;
	void* logger;
};
struct swarmkv_readoptions
{

};
struct swarmkv_writeoptions
{

};


