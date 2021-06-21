#include "swarm_kv.h"
#include "uthash/uthash.h"
#include "nng/nng.h"

struct swarmkv_value
{
	char* key;
	size_t keylen;
	char* value;
	size_t vallen;
	UT_array* nodes_have_key_cached; //for key invalidation
	UT_hash_handle hh;
};
struct swarmkv_route
{
	char* key;
	size_t keylen;
	int node_id;
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
	char table_name[256];
//	int update_interval; //bin log?
	int cache_invalidation_mode;	//Tracking or broadcasting
	struct swarmkv_value* owned_value_hash;	
	struct swarmkv_value* cached_value_hash;
	pthread_rwlock_t rwlock;
	
	struct swarmkv_route* key_routing_table;
	UT_hash_handle hh;
};
struct swarmkv_transaction
{
	char uuid[32];
	swarmkv_callback_func_t callback;
	void* usrdata;
	UT_hash_handle hh;
};
struct swarmkv_msg
{
	int msg_type;//operation
	//define struct
};
struct swarmkv_node
{
	int node_id;
	char url[256];	
};
struct swarmkv_store
{ 
	struct swarmkv_slot slots[65535];
	struct swarmkv_table* table_hash;
	nng_socket sock_listen;
	nng_socket sock;
	int node_id;
	struct swarmkv_transaction* onfly_transactions;
	pthread_rwlock_t rwlock;
};
struct swarmkv_readoptions{};
struct swarmkv_writeoptions{};
char* swarmkv_get(struct swarmkv_store*store, const char* table_name, const struct swarmkv_readoptions* options, const char* key, size_t keylen, size_t *vallen, char** errptr)
{
	int slot_id=crc(key, keylen);
	struct swarmkv_slot * slot=store->slots[slot_id];
	struct swarmkv_table* table=HASH_FIND_STR(&store->table_hash, table_name);
	struct swarmkv_value* result=NULL;
	struct swarmkv_transaction* transaction=NULL;
	char* ret_buff=NULL;
	assert(slot_id=slot->slot_id);
	if(slot->owner_node_id==store->node_id)
	{
		HASH_FIND_PTR(table->owned_value_hash, key, keylen, &result);
		if(result)
		{
			ret_buff=ALLOC(char*, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
	}
	else
	{
		transaction=ALLOC(struct swarmkv_transaction, 1);
		transaction->uuid=getuuid();
		HASH_ADD(hh, store->onfly_transactions, uuid, strlen(transaction->uuid), transaction);
		//call rpc
	}
}



