#pragma once

#include "swarmkv_limits.h"
#include "swarmkv_keyspace.h"
#include "uthash.h"
#include "utarray.h"

#include <pthread.h>

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


struct key_route_entry
{
	char* key;
	size_t keylen;
	int owner_node_id;
	UT_hash_handle hh;
};


struct swarmkv_readoptions
{

};
struct swarmkv_writeoptions
{

};


