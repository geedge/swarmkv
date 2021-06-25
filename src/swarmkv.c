#include "swarmkv.h"
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

/*
struct swarmkv_route	//abolished
{
	char* key;
	size_t keylen;
	int node_id;
	UT_hash_handle hh;
};
*/

struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
	int replica_node_id[16];
};

struct swarmkv_table
{
	char table_name[256];//Key
//	int update_interval; //bin log?
	int cache_invalidation_mode;	//Tracking or broadcasting
	struct swarmkv_value* owned_value_hash;	
	struct swarmkv_value* cached_value_hash;
	pthread_rwlock_t rwlock;
	
	//struct swarmkv_route* key_routing_table;
	UT_hash_handle hh;
};
struct swarmkv_transaction
{
	char uuid[32];
	swarmkv_callback_func_t callback;
	void* usrdata;
	UT_hash_handle hh;
};


struct swarmkv_node
{
	int node_id;//Key
	char url[256];

	uint32_t ip;
	uint32_t ping_sent;     
    uint32_t pong_received;  
    int flag;   //Online/Offline/Suspected

	UT_hash_handle hh;
};

struct swarmkv_store
{ 
	struct swarmkv_slot slots[65535];
	struct swarmkv_table* table_hash; //key: char table_name[256], value: struct swarmkv_table*
	struct swarmkv_node* nodes;		//key: int node_id, value: struct swarmkv_node*
	
	nng_socket sock_listen;
	nng_socket sock;
	int node_id;     //own id
	struct swarmkv_transaction* on_fly_transactions;
	pthread_rwlock_t rwlock;
};

struct swarmkv_readoptions{};
struct swarmkv_writeoptions{};

char* swarmkv_get(struct swarmkv_store* store, const char* table_name, const struct swarmkv_readoptions* options, const char* key, size_t keylen, size_t *vallen, char** errptr)
{
	int slot_id=-1;
	struct swarmkv_slot * slot=NULL;
	struct swarmkv_table* table=NULL;
	struct swarmkv_value* result=NULL;
	struct swarmkv_transaction* transaction=NULL;
	char* ret_buff=NULL;
	
	slot_id=crc(key, keylen);
	slot=store->slots[slot_id];
	assert(slot_id=slot->slot_id);
	if(slot->owner_node_id==store->node_id)     //if I am the owner of this slot
	{
		table=HASH_FIND_STR(&store->table_hash, table_name);
		if(!table)
		{
			//table=swarmkv_table_new();
		}
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
		HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
		//call rpc
	}
	//先检查缓存？
	else
	{
		table=HASH_FIND_STR(&store->table_hash, table_name);
		HASH_FIND_PTR(table->cached_value_hash, key, keylen, &result);
		if(result)
		{
			ret_buff=ALLOC(char*, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
		else
		{
			transaction=ALLOC(struct swarmkv_transaction, 1);
			transaction->uuid=getuuid();
			HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
			//call rpc
		}
	}
	
}
struct swarmkv_store* swarmkv_open()
{
	//1 create udp socket, which receive and send msg via UDP 8323 by default. And necessary data structures.
	//2 Get slot table, choose slots for myself
	//3 Key Migration
	// 3.1 Send slot moving request to original slot master;
	// 3.2 For original slot master:
	//	- Send the keys of this slot to new slot master; 
	//  - Mark this slot moved, read/write operations of this slot are redirected to new slot master
	//  Note: each slot is small, so that we can block r/w on sending keys.
	// 3.3 Broadcast slot master changed message in cluster
	//4 Create a thread to handle remote messages: PUT, GET, MOVE, HEALTH_CHECK, INVALIDATE_KEY, LIST_SLOT
	//return
}
void swarmkv_close()
{
	//Broadcast LEAVE message to cluster
	//Free data structures
	//return;
}
void swarmkv_msg_processing_thread()
{
	while(1)
	{
		receive_msg(sd, buff);
		msg=msgpack_unpack(buff);
		switch(msg.type)
		{
			case PUT_REQ:
				//hash_table_add(owned_value_hash, msg.key, msg.value)
				//send INVLIDATE_KEY message
				break;
			case GET_REQ:
				//value=hash_table_find(owned_value_hash, msg.key);
				//update nodes_have_key(msg.node_id,);
				//send GET_RESPONSE message
				break;
			case GET_RESPONSE:
			case PUT_RESPONSE:
				//hash_table_add(cached_value_hash, msg.key, msg.value);
				//transaction=hash_table_find(on_fly_transactions, msg.transaction_id);
				//transaction.callback()
				break;
			case HEALTH_CHECK_PING:
				//check swarmkv_node nodes;
				//send HEALTH_CHECK_PONG message;
				break;
			case HEALTH_CHECK_PONG:
				//update swarmkv_node->pong_recieved;
				//check swarmkv_node nodes;
				break;
			case MEET_REQ:
				//add the node into swarmkv_store->nodes
				//send MEET_RESP message
				break;
			case MEET_RESP:
				//read swarmkv_slot myslots[65535];
				break;
			case FAILED:
				//calculate consistent hash, update slot table
				break;
			case MOVE_REQ:
				//see swarmkv_open 3.2
				break;
			case MOVE_RESP:
				//update swarmkv_value* owned_value_hash;
				break;
			case HEALTH_CHECK:
				//do gossip
				break;
			case INVALIDATE_KEY:
				//delete from cached_value_hash;
				break;
			
		}
		//Failover of cluster node
			//send failed_msg to other node
			//read swarmkv_store->slots, find the slots belonging to this failed node;
			//calculate consistent hash, update slot table
			//Notify other nodes of the new slot table?或者仅仅是通知其他节点我发现哪个节点挂了，让他们自己重新计算slot table？
	}
}
