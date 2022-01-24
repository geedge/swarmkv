#pragma once

#include "swarmkv_limits.h"
#include "uthash/uthash.h"
#include <pthread.h>
#include <netinet/ip.h>

enum swarmkv_value_type
{
	value_type_string,
	value_type_integer,
	value_type_hashtable
};
enum slot_state
{
	slot_unkonwn,
	slot_stable,
	slot_mirgrating
};

struct swarmkv_key_value
{
	enum swarmkv_value_type value_type;
	char* key;
	size_t keylen;
	char* value;
	size_t vallen;
};
struct swarmkv_key_value_container
{
	struct swarmkv_key_value head;
	UT_hash_handle hh;
};

//slot is an storage concept for partition KV to cluster nodes.
struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
	int replica_node_ids[MAX_REPLICA_NUM];
	enum slot_state state;
	
	struct swarmkv_key_value *hash_own_kv;
	struct swarmkv_key_value *hash_cached_kv;
	struct swarmkv_key_value *hash_replica_kv;
	pthread_rwlock_t rwlock;
};
struct swarmkv_node
{
	struct sockaddr_in peer_addr;	//man 7 ip
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
struct swarmkv_store
{
	struct swarmkv_params para;
	pthread_t thread;
	struct swarmkv_slot slots[SLOT_NUM];
};


