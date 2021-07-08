#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h> 
#include <net/if.h>  
#include <netdb.h>  
#include <sys/types.h>  
#include <sys/ioctl.h> 
#include <cjson/cJSON.h> 

#include <pthread.h>

#include "../include/swarmkv.h"
#include "./inc_internal/swarmkv_internal.h"
#include "./inc_internal/swarmkv_net.h"
#include "./inc_internal/swarmkv_message.h"
#include "./inc_internal/uthash/uthash.h"
#include "./inc_internal/uthash/utarray.h"
#include "./inc_internal/swarmkv_utils.h"


#define BUFSIZE 2048
#define SWARMKV_DEFAULT_LISTEN_PORT 8323


#define ON 1
#define PFAIL 0
#define FAIL -1
#define SWARMKV_DEFAULT_NODE_TIMEOUT 15000000
#define SWARMKV_PING_INTERVAL 100000   //100ms



static const uint32_t crc32tab[] = {
 0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL,
 0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
 0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L,
 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
 0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
 0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
 0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L,
 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
 0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L,
 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
 0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L,
 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
 0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
 0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L,
 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
 0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
 0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
 0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL,
 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
 0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
 0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
 0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L,
 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
 0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L,
 0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
 0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL,
 0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
 0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
 0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
 0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL,
 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
 0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L,
 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
 0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L,
 0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
 0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
 0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
 0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
 0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L,
 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
 0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL,
 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
 0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
 0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
 0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L,
 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
 0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L,
 0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
 0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL 
};

uint32_t crc32( const unsigned char *buf, uint32_t size)
{
     uint32_t i, crc;
     crc = 0xFFFFFFFF;
     for (i = 0; i < size; i++)
      crc = crc32tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
     return crc^0xFFFFFFFF;
}

/*
//set node address
struct sockaddr_in set_node_addr(struct swarmkv_node* node)
{
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(node->port);
	addr.sin_addr.s_addr = inet_addr(node->ip);

	return addr;
}
*/


//write the key-value into my store 
int swarmkv_owner_put_kv(struct swarmkv_store *store, char* tb_name, char* key, char* value)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	pthread_rwlock_wrlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//add a new table in my swarmkv_store
		table = ALLOC(struct swarmkv_table, 1);
		memcpy(table->table_name, tb_name, strlen(tb_name));
		HASH_ADD_STR(store->table_hash, table_name, table);
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = key;
		HASH_ADD_STR(table->owned_value_hash, key, kv);
	}
	HASH_FIND_STR(table->owned_value_hash, key, kv);
	if(kv == NULL)
	{
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = key;
		HASH_ADD_STR(table->owned_value_hash, key, kv);
	}
	kv->keylen = strlen(kv->key);
	kv->value = value;
	kv->vallen = strlen(kv->value);

	pthread_rwlock_unlock(&store->rwlock);

	return 1;
}

//send an invalidate request msg to nodes in nodes_have_key_cached
int swarmkv_msg_send_invalidate_key_req(struct swarmkv_store *store, char* tb_name, char* key, char* value)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;

	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		printf("I don't have the table %s\n", tb_name);
		return -1;
	}
	HASH_FIND_STR(table->owned_value_hash, key, kv);
	if(kv == NULL)
	{
		printf("I don't have the key %s in the table %s\n", key, tb_name);
		return -1;
	}
	int* cached_node_id;
	struct swarmkv_node* cache_node = NULL;
	for(cached_node_id=(int*)utarray_front(kv->nodes_have_key_cached); cached_node_id!=NULL; cached_node_id=(int*)utarray_next(kv->nodes_have_key_cached,cached_node_id)) 
	{
		HASH_FIND_INT(store->nodes, cached_node_id, cache_node);
		if(cache_node != NULL)
		{
			char *invalidate_key_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_update_key_req(store, MSG_INVALIDATE_KEYS_REQ, invalidate_key_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key, value);
			//swarmkv_msg_pack(store, MSG_INVALIDATE_KEYS_REQ, invalidate_key_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key, value, -1);
			if(msg_len <= 0)
			{
				printf("Pack invalidate key request message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, invalidate_key_req_msg, cache_node->ip, cache_node->port);
			free(invalidate_key_req_msg);
			invalidate_key_req_msg = NULL;
		}
	}
	return 1;
}

//caculate the slot owner id
int caculate_key_slot_owner(char* key, struct swarmkv_store *store)
{
	struct swarmkv_slot slot;
	int slot_id = -1;
	slot_id = crc32(key,strlen(key))%SWARMKV_DEFAULT_SLOT_NUM;
	slot = store->slots[slot_id];
	assert(slot_id == slot.slot_id);
	return slot.owner_node_id;
}

//put
int swarmkv_put(char* tb_name, char* key, char* value, struct swarmkv_store *store)
{
	int slot_owner_id = -1;
	slot_owner_id = caculate_key_slot_owner(key, store);
	if(slot_owner_id == store->my_node_id)
	{
		//local put a key-value
		swarmkv_owner_put_kv(store, tb_name, key, value);
		swarmkv_msg_send_invalidate_key_req(store, tb_name, key, value);
	}
	else
	{
		struct swarmkv_node* peer_node = NULL;
		HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
		if(peer_node == NULL)
		{
			printf("I can't find the owner_node of the key %s!\n", key);
			return -1;
		}
		char* put_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_update_key_req(store, MSG_PUT_REQ, put_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key, value);
		//swarmkv_msg_pack(store, MSG_PUT_REQ, put_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key, value, -1);
		if(msg_len <= 0)
		{
			printf("Pack put request message error.\n");
			return -1;
		}
		swarmkv_msg_send(store->sockfd, put_req_msg, peer_node->ip, peer_node->port);
		free(put_req_msg);
		put_req_msg= NULL;
	}
	return 1;
}

//get a key-value from my store as owner
char* swarmkv_owner_get_kv(struct swarmkv_store *store, char* tb_name, char* key, int req_node_id)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I can't find the table!\n");
		return NULL;
	}
	HASH_FIND_STR(table->owned_value_hash, key, kv);
	if(kv == NULL)
	{
		//printf("I can't find the key!\n");
		return NULL;
	}
	//在UT_array *nodes_have_key_cached里插入这个查询的节点
	if(req_node_id != store->my_node_id)
	{
		utarray_push_back(kv->nodes_have_key_cached, &req_node_id);
	}
	pthread_rwlock_unlock(&store->rwlock);
	return kv->value;
}

char* swarmkv_cache_get_kv(struct swarmkv_store *store, char* tb_name, char* key)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I can't find the table in my cache!\n");
		return NULL;
	}
	HASH_FIND_STR(table->cached_value_hash, key, kv);
	if(kv == NULL)
	{
		//printf("I can't find the key in my cache!\n");
		return NULL;
	}
	pthread_rwlock_unlock(&store->rwlock);
	return kv->value;
}

//get keys
char* swarmkv_get(struct swarmkv_store *store, char* tb_name, char* key, char* cb_function)
{
	int slot_owner_id = -1;
	char* value = NULL;
	
	slot_owner_id = caculate_key_slot_owner(key, store);
	
	//I am the owner of the key
	if(slot_owner_id == store->my_node_id)
	{
		value = swarmkv_owner_get_kv(store, tb_name, key, slot_owner_id);
		//printf("%s: %s\n", key[i], value);
	}
	else
	{
		//check my cache
		value = swarmkv_cache_get_kv(store, tb_name, key);
		//I have not cache the kv
		if(value == NULL)
		{
			//send a get request
			struct swarmkv_node* peer_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
			if(peer_node == NULL)
			{
				printf("I can't find the owner_node of the key %s!\n", key);	
			}
			char *get_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_get_req(store, get_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key, cb_function);
			//swarmkv_msg_pack_get_req(store, tb_name, key, cb_function);
			if(msg_len <= 0)
			{
				printf("Pack get requerst message error.\n");
				return NULL;
			}
			swarmkv_msg_send(store->sockfd, get_req_msg, peer_node->ip, peer_node->port);
			free(get_req_msg);
			get_req_msg = NULL;
		}
	}
	return value;
}

//parse the node list and slot list in ping/pong/meet_resp msg
int swarmkv_parse_node_and_slot(struct swarmkv_store *store, cJSON* node_list, cJSON* slot_list)
{
	int node_num = cJSON_GetArraySize(node_list);
	cJSON *node_item;
	int i = 0;
	for (i = 0; i < node_num; i++)
	{
		node_item = cJSON_GetArrayItem(node_list, i);
		cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
		cJSON *node_status = cJSON_GetObjectItem(node_item, "node_status");
		int id = node_id->valueint;

		struct swarmkv_node *new_node = NULL;
		HASH_FIND_INT(store->nodes, &id, new_node);
		//add new nodes into my store->nodes
		if (new_node == NULL)
		{
			printf("add a new node from ping\n");
			new_node = ALLOC(struct swarmkv_node, 1);
			new_node->node_id = id;
			cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
			cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");
			new_node->ip = node_ip->valuestring;
			new_node->port = node_port->valueint;
			new_node->status = node_status->valueint;
			new_node->ping_sent_t = 0;
			new_node->pong_received_t = 0;
			new_node->pfail_cnt = 0;
			HASH_ADD_INT(store->nodes, node_id, new_node);
		}
		//check the exist node status
		else
		{
			//if the node is failed, update the node status
			if (node_status->valueint == FAIL)
			{
				new_node->status = FAIL;
			}
			//if the node is pfailed, update the node status and increase the pfail_cnt
			if (node_status->valueint == PFAIL)
			{
				new_node->status = PFAIL;
				new_node->pfail_cnt++;
				
				//Check if more than half of the nodes think that the current node is pfail
				int node_cnt = HASH_COUNT(store->nodes);
				if(new_node->pfail_cnt > node_cnt/2)
				{
					new_node->status = FAIL;
					new_node->pfail_cnt = 0;
					//broadcast a fail msg
					char *fail_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
					size_t msg_len = swarmkv_msg_pack_fail_req(store, fail_req_msg,  SWARMKV_MAX_MSG_SIZE, id);
					//swarmkv_msg_pack_fail_req(store, id);
					if(msg_len <= 0)
					{
						printf("Pack fail request message error.\n");
						return -1;
					}
					struct swarmkv_node* node = NULL;
					for(node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) 
					{
						swarmkv_msg_send(store->sockfd, fail_req_msg, node->ip, node->port);
					}
					free(fail_req_msg);
					fail_req_msg = NULL;
				}			
			}
		}
	}
	int slot_num = cJSON_GetArraySize(slot_list);
	cJSON *slot_item;
	for (i = 0; i < slot_num; i++)
	{
		slot_item = cJSON_GetArrayItem(slot_list, i);
		cJSON *slot_id = cJSON_GetObjectItem(slot_item, "slot_id");
		cJSON *owner_id = cJSON_GetObjectItem(slot_item, "owner_id");
		if (store->slots[slot_id->valueint].owner_node_id != owner_id->valueint)
		{
			store->slots[slot_id->valueint].owner_node_id = owner_id->valueint;
			//Haven't consider the problem of slot backup nodes.
			//需要添加epoch进行控制，不然不知道到底谁的版本才是最新的，可能因为网络延迟，旧的slot版本反而后到
		}
	}
	return 1;
}

typedef void (*swarmkv_cb_function)(char*, char*);

void print_result(char* key, char* value)
{
	printf("get result\n%s: %s\n", key, value);
}

int swarmkv_unpack_msg(char* buf, struct swarmkv_store* store)
{
    cJSON* msg = cJSON_Parse(buf);
	if(!msg) {
    	printf("get msg faild !\n");
		return -1;
	}
	cJSON* msg_type = cJSON_GetObjectItem(msg, "msg_type");
	cJSON* src_id = cJSON_GetObjectItem(msg, "my_id");
	cJSON* src_ip = cJSON_GetObjectItem(msg, "my_ip");
	cJSON* src_port = cJSON_GetObjectItem(msg, "my_port");
	cJSON* tb_name = 0;
	cJSON* key = 0;
	cJSON* value = 0;
	cJSON* cb_function = 0;
	cJSON* node_list = 0;
	cJSON* slot_list = 0;
	int id = src_id->valueint;

	struct swarmkv_node *src_node = NULL;
	HASH_FIND_INT(store->nodes, &id, src_node); 

	size_t msg_len = 0;

	switch(msg_type->valueint)
	{
		case MSG_PING:
		case MSG_PONG:
			//check if the src node is legal
			if(src_node == NULL)
			{
				printf("It is an UNKNOWN node %d!\n", id);
				return -1;
			}
			
			if(msg_type->valueint == MSG_PING)
			{
				printf("it is a PING msg\n");
			}
			else if((msg_type->valueint == MSG_PONG))
			{
				src_node->pong_received_t = clock();
				printf("it is a PONG msg\n");
			}
				
			//check if the msg is complete
			node_list = cJSON_GetObjectItem(msg, "node_list");
			if(!node_list)
			{
				printf("no node list!\n");
				return -1;
			}
			slot_list = cJSON_GetObjectItem(msg, "slot_list");
			if(!node_list)
			{
				printf("no slot list!\n");
				return -1;
			}
	
			//update the status of src_node to ON
			if(src_node->status == PFAIL)
			{
				src_node->status = ON;
				src_node->pfail_cnt = 0;
			}
			swarmkv_parse_node_and_slot(store, node_list, slot_list);

			//send response msg
			char* resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			if(msg_type->valueint == MSG_PING)
			{
				msg_len = swarmkv_msg_pack_health_check(store, MSG_PONG, resp_msg, SWARMKV_MAX_MSG_SIZE);
				if(msg_len <= 0)
				{
					printf("Pack pong message error\n");
					return -1;
				}
			}
			if(msg_type->valueint == MSG_PONG)
			{
				msg_len = swarmkv_msg_pack_health_check(store, MSG_PING, resp_msg, SWARMKV_MAX_MSG_SIZE);
				if(msg_len <= 0)
				{
					printf("Pack ping message error\n");
					return -1;
				}
				src_node->ping_sent_t = clock();
			}
			swarmkv_msg_send(store->sockfd, resp_msg, src_node->ip, src_node->port);
			free(resp_msg);
			resp_msg = NULL;

			printf("Press enter to continue...\n");
			getchar();

			break;

		case MSG_MEET_RESP:
			printf("it is a MEET RESPONSE msg\n");
			
			if(src_node == NULL)
			{
				src_node = ALLOC(struct swarmkv_node, 1);
				src_node->node_id = id;
				HASH_ADD_INT(store->nodes, node_id, src_node);
			}
			src_node->ip = src_ip->valuestring;
			src_node->port = src_port->valueint;
			src_node->ping_sent_t = 0;
			src_node->pong_received_t = 0;
			src_node->status = ON;
			src_node->pfail_cnt = 0;

			//check if the msg is complete
			node_list = cJSON_GetObjectItem(msg, "node_list");
			if(!node_list)
			{
				printf("no node list!\n");
				return -1;
			}
			slot_list = cJSON_GetObjectItem(msg, "slot_list");
			if(!slot_list)
			{
				printf("no slot list!\n");
				return -1;
			}
			
			swarmkv_parse_node_and_slot(store, node_list, slot_list);

			printf("Press enter to continue...\n");
			getchar();
			break;

		case MSG_MEET_REQ:
			printf("it is a MEET REQUEST msg\n");
			//int id = src_id->valueint;
			struct swarmkv_node *new_node;
    		HASH_FIND_INT(store->nodes, &id, new_node); 
			if(new_node == NULL)
			{
				printf("meet a new node\n");
				new_node = ALLOC(struct swarmkv_node, 1);
				new_node->node_id = id;
				HASH_ADD_INT(store->nodes, node_id, new_node);
			}
			else
			{
				printf("meet a exist failed node\n");
			}
			new_node->ip = src_ip->valuestring;
			new_node->port = src_port->valueint;
			new_node->ping_sent_t = 0;
			new_node->pong_received_t = 0;
			new_node->status = ON;
			new_node->pfail_cnt = 0;

			//send a meet response msg
			char *meet_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			msg_len = swarmkv_msg_pack_health_check(store, MSG_MEET_RESP, resp_msg, SWARMKV_MAX_MSG_SIZE);
			//swarmkv_msg_pack_health_check(store, MSG_MEET_RESP);
			if(msg_len <= 0)
			{
				printf("Pack meet response error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, meet_resp_msg, new_node->ip, new_node->port);
			free(meet_resp_msg);
			meet_resp_msg = NULL;

			printf("Press enter to continue...\n");
			getchar();

			break;

		case MSG_FAIL_REQ:
			//check if the src node is legal
			if(src_node == NULL)
			{
				printf("It is an UNKNOWN node %d!\n", id);
				return -1;
			}
			printf("It is a FAIL msg\n");
			//find the fail node
			struct swarmkv_node *fail_node = NULL;
			cJSON* fail_node_id = cJSON_GetObjectItem(msg, "fail_node_id");
			int fail_id = fail_node_id->valueint;
			HASH_FIND_INT(store->nodes, &fail_id, fail_node); 
			if(fail_node != NULL)
			{
				fail_node->status = FAIL;
				fail_node->pfail_cnt = 0;
				//send a fail response msg
				char *fail_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				msg_len = swarmkv_msg_pack_ok_response(store, MSG_FAIL_RESP, fail_resp_msg, SWARMKV_MAX_MSG_SIZE);
				if(msg_len <= 0)
				{
					printf("pack fail response message error.\n");
					return -1;
				}
				swarmkv_msg_send(store->sockfd, fail_resp_msg, src_node->ip, src_node->port);
				free(fail_resp_msg);
				fail_resp_msg = NULL;
			}
			break;

		case MSG_FAIL_RESP:
			break;

		case MSG_PUT_REQ:
			if(src_node == NULL)
			{
				printf("It is a PUT REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");

			//put the key-value into my store and send invalidate request msg
			swarmkv_owner_put_kv(store, tb_name->valuestring, key->valuestring, value->valuestring);
			swarmkv_msg_send_invalidate_key_req(store, tb_name->valuestring, key->valuestring, value->valuestring);
			//send a put response "ok"
			char *put_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			msg_len = swarmkv_msg_pack_ok_response(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE);
			if(msg_len <= 0)
			{
				printf("pack put response message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, put_resp_msg, src_node->ip, src_node->port);
			free(put_resp_msg);
			put_resp_msg = NULL;

			break;

		case MSG_PUT_RESP:
			break;
		
		case MSG_INVALIDATE_KEYS_REQ:
			//update the cached_value_hash
			if(src_node == NULL)
			{
				printf("It is a INVALIDATE KEYS msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");
	
			struct swarmkv_table* table = NULL;
			struct swarmkv_key_value* cache_kv = NULL;
			HASH_FIND_STR(store->table_hash, tb_name->valuestring, table);
			if(table != NULL)
			{
				HASH_FIND_STR(table->cached_value_hash, key->valuestring, cache_kv);
				if(cache_kv != NULL)
				{
					cache_kv->value = value->valuestring;
					cache_kv->vallen = strlen(cache_kv->value);
					//send a invalidate key response "ok"
					char *invalidate_key_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
					msg_len = swarmkv_msg_pack_ok_response(store, MSG_INVALIDATE_KEYS_RESP, invalidate_key_resp_msg, SWARMKV_MAX_MSG_SIZE);
					if(msg_len <= 0)
					{
						printf("pack put response message error.\n");
						return -1;
					}
					swarmkv_msg_send(store->sockfd, invalidate_key_resp_msg, src_node->ip, src_node->port);
					free(invalidate_key_resp_msg);
					invalidate_key_resp_msg = NULL;
				}
			}
			break;
		case MSG_INVALIDATE_KEYS_RESP:
			break;
		
		case MSG_GET_REQ:
			if(src_node == NULL)
			{
				printf("It is a GET REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			cb_function = cJSON_GetObjectItem(msg, "callback");
			char* val = NULL;
			val = swarmkv_owner_get_kv(store, tb_name->valuestring, key->valuestring, src_node->node_id);

			//send a response
			char *get_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			msg_len = swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, tb_name->valuestring, key->valuestring, val, cb_function->valuestring);
			if(msg_len <= 0 )
			{
				printf("pack a get response message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, get_resp_msg, src_node->ip, src_node->port);
			free(get_resp_msg);
			get_resp_msg = NULL;
			break;

		case MSG_GET_RESP:
			if(src_node == NULL)
			{
				printf("It is a GET REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");
			cb_function = cJSON_GetObjectItem(msg, "callback");

			//update my cache
			struct swarmkv_table *tb = NULL;
			struct swarmkv_key_value *kv = NULL;
			pthread_rwlock_wrlock(&store->rwlock);
			HASH_FIND_STR(store->table_hash, tb_name->valuestring, tb);
			if(tb == NULL)
			{
				//add a new table in my swarmkv_store
				tb = ALLOC(struct swarmkv_table, 1);
				memcpy(tb->table_name, tb_name->valuestring, strlen(tb_name->valuestring));
				HASH_ADD_STR(store->table_hash, table_name, tb);
				//add a new key
				kv = ALLOC(struct swarmkv_key_value, 1);
				kv->key = key->valuestring;
				HASH_ADD_STR(tb->cached_value_hash, key, kv);
			}
			HASH_FIND_STR(tb->cached_value_hash, key->valuestring, kv);
			if(kv == NULL)
			{
				//add a new key
				kv = ALLOC(struct swarmkv_key_value, 1);
				kv->key = key->valuestring;
				HASH_ADD_STR(tb->cached_value_hash, key, kv);
			}
			kv->keylen = strlen(kv->key);
			kv->value = value->valuestring;
			kv->vallen = strlen(kv->value);
			//callback
			/*
			swarmkv_cb_function cb_f = NULL;
			cb_f = (swarmkv_cb_function)cb_function->valuestring;
			if(cb_f!=NULL)
			{
				(*cb_f)(key->valuestring, value->valuestring);
			}
			*/
			break;
		default:
			printf("UNKNOWN msg\n");
	}

	return 1;
}

//check cluster nodes status
int swarmkv_random_ping(int sockfd, struct swarmkv_store *store)
{
	int i = 0;
	int nodes_cnt = HASH_COUNT(store->nodes);
	//printf("%d\n", nodes_cnt);
	uint32_t min_pong = 0;
	struct swarmkv_node *min_pong_node = NULL;

	//select a node to send ping msg
	int choose_in = 5;
	if(nodes_cnt < choose_in)
	{
		choose_in=nodes_cnt;
	}
	for (i = 0; i < choose_in; i++)
	{
		int node_id = rand()%nodes_cnt;
		if(node_id == store->my_node_id)
		{
			continue;
		}
		struct swarmkv_node *candidate_node = NULL;
		candidate_node = ALLOC(struct swarmkv_node, 1);
		HASH_FIND_INT(store->nodes, &node_id, candidate_node);
		if(candidate_node == NULL)
		{
			continue;
		}
		if(candidate_node->ping_sent_t!=0)
		{
			//recently sent a ping, and did not receive the corresponding pong.
			continue;
		}
		if(min_pong_node == NULL || min_pong > candidate_node->pong_received_t)
		{
			min_pong = candidate_node->pong_received_t;
			min_pong_node = candidate_node;
		}
	}
	if(min_pong_node)
	{
		printf("send a ping to random node %d\n", min_pong_node->node_id);
		char *ping_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_health_check(store, MSG_PING, ping_msg, SWARMKV_MAX_MSG_SIZE);
		if(msg_len <= 0)
		{
			printf("pack a ping message error\n");
			return -1;
		}
		swarmkv_msg_send(store->sockfd, ping_msg, min_pong_node->ip, min_pong_node->port);
		min_pong_node->ping_sent_t = clock();
		free(ping_msg);
		ping_msg = NULL;
	}
	return 1;
}

/*
int swarmkv_regular_ping(struct swarmkv_store *store, clock_t cur_t)
{
	struct swarmkv_node *node = NULL;

	for (node = store->nodes; node != NULL; node = (struct swarmkv_node *)(node->hh.next))
	{
		//do not send ping msg to myself
		if (node->node_id == store->my_node_id)
		{
			continue;
		}
		if (node->status == FAIL)
		{
			continue;
		}
		//if there is a node: current_time - pong_sent_t >timeout/2, send a ping msg immediately
		if (node->pong_received_t == 0 || cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT / 2)
		{
			//printf("send a ping to node %d\n", node->node_id);
			char *ping_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_health_check(store, MSG_PING, ping_msg, SWARMKV_MAX_MSG_SIZE);
			if(msg_len <= 0)
			{
				printf("Pack ping msg error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, ping_msg, node->ip, node->port);
			node->ping_sent_t = clock();
			free(ping_msg);
			ping_msg = NULL;
			continue;
		}
		//if there is a node: current_time - pong_sent_t >timeout, suspect that the node is failed
		if (cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT)
		{
			printf("set a node %d pfail\n", node->node_id);
			node->status = PFAIL;
		}	
	}
	return 1;
}
*/

void *swarmkv_msg_processing_thread(void *arg)
{
	struct swarmkv_store *store = (struct swarmkv_store *) arg;
	char recvbuf[BUFSIZ];
	struct sockaddr_in peer_addr;
	socklen_t peerlen = sizeof(struct sockaddr_in);
	int msg_cnt = 0;
	int ping_cnt = 0;
	int check_cnt = 0;
	while(1)
	{
		memset(recvbuf, 0, sizeof(recvbuf));
		int n = recvfrom(store->sockfd, recvbuf, BUFSIZ, 0, (struct sockaddr *)&peer_addr, &peerlen);
		if(n > 0) 
		{
			//msg_cnt++;
			//printf("recieve %d msg\n", msg_cnt);
			printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
			swarmkv_unpack_msg(recvbuf, store);	
		}
		clock_t cur_t = clock();
		struct swarmkv_node* node = NULL;
		
		//Traverse all nodes per 100ms
		if(cur_t/SWARMKV_PING_INTERVAL >= check_cnt)
		{
			check_cnt++;
			//swarmkv_regular_ping(store, cur_t);
			for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) 
			{
				//do not send ping msg to myself
				if(node->node_id == store->my_node_id)
				{
					continue;
				}
				if(node->status == FAIL)
				{
					continue;
				}
				if(node->pong_received_t == 0)
				{
					//printf("send a ping to node %d\n", node->node_id);
					char *ping_msg = swarmkv_msg_pack_health_check(store, MSG_PING);
					swarmkv_msg_send(store->sockfd, ping_msg, node->ip, node->port);
					continue;
				}
				//if there is a node: current_time - pong_sent_t >timeout/2, send a ping msg immediately
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT/2)
				{
					printf("send a ping to node %d\n", node->node_id);
					char *ping_msg = swarmkv_msg_pack_health_check(store, MSG_PING);
					swarmkv_msg_send(store->sockfd, ping_msg, node->ip, node->port);
					continue;
				}
				//if there is a node: current_time - pong_sent_t >timeout, suspect that the node is failed
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT)
				{
					printf("set a node %d pfail\n", node->node_id);
					node->status = PFAIL;
				}
    		}
		}
		//ping a random node per second
		if(cur_t/1000000 >= ping_cnt)
		{
			//printf("select a random node\n");
			ping_cnt++;
			swarmkv_random_ping(store->sockfd, store);
		}	
	}
	pthread_exit(NULL);
}

int swarmkv_config_and_init(struct swarmkv_store *store, const char* bootstraps, const char* config)
{
	struct bootstrap_node *bs_nodes;

	int bs_info_len = strlen(bootstraps);
	char *bootstrap_info = ALLOC(char, bs_info_len);
	memcpy(bootstrap_info, bootstraps, bs_info_len);

	int config_info_len = strlen(config);
	char* config_info = ALLOC(char, config_info_len);
	memcpy(config_info, config, config_info_len);
	
	char *my_id_info;
	char *my_info = NULL;
	char *peer_node_info = NULL;
    char *p;
	char *p1;
	char *peer_ip;
	uint16_t peer_port;

	my_id_info = strtok_r(config_info, ";", &p);
	if(my_id_info != NULL)
	{
		strtok_r(my_id_info, "=", &p);
		store->my_node_id = atoi(strtok_r(NULL, "=", &p));
		printf("%d\n", store->my_node_id);
	}

	my_info = strtok_r(bootstrap_info, ";", &p);
	strtok_r(my_info, "=", &p1);
	my_info = strtok_r(NULL, "=", &p1);
	printf("my_info: %s\n",my_info);
	peer_node_info = strtok_r(NULL, ";", &p);
	strtok_r(peer_node_info, "=", &p1);
	peer_node_info = strtok_r(NULL, ";", &p1); 
	printf("peer_node_info: %s\n",peer_node_info);

    // my_info example 172.16.224.160:8323
	if(my_info != NULL)
	{
		store->my_ip = strtok_r(my_info, ":", &p);
		store->my_port = atoi(strtok_r(NULL, ":", &p));
		//printf("store->my_ip: %s\n", store->my_ip);
		//printf("store->my_port: %d\n", store->my_port);
	}
	//create socket
	store->sockfd = swarmkv_create_socket(store->my_port);
	printf("store->sockfd : %d\n", store->sockfd);

	//parse bootstrap nodes and send meet requests
	char* peer_nodes;
	int i = 0;
	// peer_node_info example 172.16.224.160:6323,172.16.224.160:7323,172.16.224.160:18323
	if(peer_node_info != NULL)
	{
		peer_node_info = strtok_r(peer_node_info, ",", &p);
	}
	
	while(peer_node_info != NULL)
	{
		//peer_nodes = peer_node_info;
		printf("%s\n",peer_node_info);
		peer_ip = strtok_r(peer_node_info, ":", &p1);
		peer_port = atoi(strtok_r(NULL, ":", &p1));
		//printf("peer_ip: %s\n", peer_ip);
		//printf("peer_port: %d\n", peer_port);
		
		//send meet msg to each bootstrap node
		char *meet_req = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_meet_req(store, meet_req, SWARMKV_MAX_MSG_SIZE);
		if(msg_len <= 0)
		{
			printf("Pack meet request message to ip %s error.\n", peer_ip);
			return -1;
		}
		swarmkv_msg_send(store->sockfd, meet_req, peer_ip, peer_port);
		free(meet_req);
		meet_req = NULL;
		peer_node_info = strtok_r(NULL, ",", &p);
	}
	return 1;
}

struct swarmkv_store *swarmkv_open(const char* bootstraps, const char* config, char* err)
{
	//parse my_node_id from config
	//parse my port from bootstraps
	struct swarmkv_store *store = NULL;
	store = ALLOC(struct swarmkv_store, 1);

	printf("open a node\n");

	//init, set my info(ip.port,id), parse bootstrap node information and send meet msg to them
	swarmkv_config_and_init(store, bootstraps, config);

	//init rwlock
	pthread_rwlock_init(&store->rwlock, NULL);

	//add myself to store->nodes
	struct swarmkv_node* myself = NULL;
	myself = ALLOC(struct swarmkv_node, 1);
	myself->node_id = store->my_node_id;
	myself->ip = store->my_ip;
	myself->port = store->my_port;
	myself->ping_sent_t = 0;
	myself->pong_received_t = 0;
	myself->status = ON;
	myself->pfail_cnt = 0;
	HASH_ADD_INT(store->nodes, node_id, myself); 
	
	//create a thread to recieve and handle remote message
	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, swarmkv_msg_processing_thread, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return NULL;
	}

	// wait for processing thread close
	if (pthread_join(tid, NULL))                  
    {
        printf("thread is not exit...\n");
	}
	pthread_exit(NULL);

	close(store->sockfd);
	return store;
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

/*
char *swarmkv_get(struct swarmkv_store *store, const char *table_name, const struct swarmkv_readoptions *options, const char *key, size_t keylen, size_t *vallen, char **errptr)
{
	int slot_id = -1;
	struct swarmkv_slot *slot = NULL;
	struct swarmkv_table *table = NULL;
	struct swarmkv_value *result = NULL;
	struct swarmkv_transaction *transaction = NULL;
	char *ret_buff = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	slot_id = crc(key, keylen);
	slot = store->slots[slot_id];
	assert(slot_id = slot->slot_id);
	if (slot->owner_node_id == store->node_id) //if I am the owner of this slot
	{
		table = HASH_FIND_STR(&store->table_hash, table_name);
		if (!table)
		{
			//table=swarmkv_table_new();
		}
		HASH_FIND_PTR(table->owned_value_hash, key, keylen, &result);
		if (result)
		{
			ret_buff = ALLOC(char, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
	}
	else
	{
		transaction = ALLOC(struct swarmkv_transaction, 1);
		transaction->uuid = getuuid();
		HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
		//call rpc
	}
	//先检查缓存？
	else
	{
		table = HASH_FIND_STR(&store->table_hash, table_name);
		HASH_FIND_PTR(table->cached_value_hash, key, keylen, &result);
		if (result)
		{
			ret_buff = ALLOC(char *, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
		else
		{
			transaction = ALLOC(struct swarmkv_transaction, 1);
			transaction->uuid = getuuid();
			HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
			//call rpc
		}
	}
	pthread_rwlock_unlock(&store->rwlock);
}
*/

void swarmkv_close()
{
	//Broadcast LEAVE message to cluster
	//Free data structures
	//return;
}

/*
void swarmkv_msg_processing_thread()
{
	while (1)
	{
		receive_msg(sd, buff);
		msg = msgpack_unpack(buff);
		switch (msg.type)
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
*/

int main(int argc, char **argv)
{
	//printf("%d\n",argc);
	const char* bootstraps = "self=172.16.224.160:8323;peers=172.16.224.160:6323,172.16.224.160:7323,172.16.224.160:18323";
	const char* config = "node_id=0;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	char* err = NULL;
	swarmkv_open(bootstraps, config, err);
	/*
	if(argc == 4)
	{
		int port = atoi(argv[1]);
		//printf("%s\n",argv[2]);
		char *eth_inf = argv[2];
		int node_id = atoi(argv[3]);
		swarmkv_open(bootstraps, config, err);
	}
	else
	{
		printf("arg error\n");
	}*/

	return 1;
}