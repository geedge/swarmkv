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

#include "swarmkv.h"
#include "swarmkv_internal.h"
#include "swarmkv_net.h"
#include "swarmkv_message.h"
#include "uthash/uthash.h"
#include "uthash/utarray.h"
#include "swarmkv_base64.h"
#include "swarmkv_crc32.h"
#include "swarmkv_conhash.h"

#define BUFSIZE 2048
#define SWARMKV_DEFAULT_LISTEN_PORT 8323
#define SWARMKV_MAX_KEY_LEN 512
#define SWARMKV_MAX_VAL_LEN 2048

#define SWARMKV_DEFAULT_NODE_TIMEOUT 15000000
#define SWARMKV_PING_INTERVAL 100000   //100ms


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
void swarmkv_owner_put_kv(struct swarmkv_store *store, const char *table_name, const char *key, size_t keylen, const char *value, size_t vallen)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	pthread_rwlock_wrlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, table_name, table);
	if(table == NULL)
	{
		//add a new table in my swarmkv_store
		table = ALLOC(struct swarmkv_table, 1);
		memcpy(table->table_name, table_name, strlen(table_name));
		HASH_ADD_STR(store->table_hash, table_name, table);
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = (char *)key;
		HASH_ADD(hh, table->owned_value_hash, key, keylen, kv);
	}
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = (char *)key;
		HASH_ADD(hh, table->owned_value_hash, key, keylen, kv);
	}
	kv->keylen = keylen;
	kv->value = (char *)value;
	kv->vallen = vallen;

	pthread_rwlock_unlock(&store->rwlock);
}

//send an invalidate request msg to nodes in nodes_have_key_cached
int swarmkv_msg_send_invalidate_key_req(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, 
											const char *value, size_t vallen)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;

	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		printf("I don't have the table %s\n", tb_name);
		return -1;
	}
	//HASH_FIND_STR(table->owned_value_hash, key, kv);
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		printf("I don't have the key %s in the table %s\n", key, tb_name);
		return -1;
	}

	//base64 encode
	char *key_base64 = ALLOC(char, keylen*2);
	char *value_base64 = ALLOC(char, vallen*2);
	base64_encode(key, keylen, key_base64);
	base64_encode(value, vallen, value_base64);

	int* cached_node_id;
	struct swarmkv_node* cache_node = NULL;
	for(cached_node_id=(int*)utarray_front(kv->nodes_have_key_cached); cached_node_id!=NULL; cached_node_id=(int*)utarray_next(kv->nodes_have_key_cached,cached_node_id)) 
	{
		HASH_FIND_INT(store->nodes, cached_node_id, cache_node);
		if(cache_node != NULL)
		{
			char *invalidate_key_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_put_req(store, MSG_INVALIDATE_KEYS_REQ, invalidate_key_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key_base64, value_base64);
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
	free(key_base64);
	key_base64 = NULL;
	free(value_base64);
	value_base64 = NULL;
	return 1;
}

//caculate the slot owner id
int caculate_key_slot_owner(struct swarmkv_store *store, const char *key, size_t keylen)
{
	struct swarmkv_slot slot;
	int slot_id = -1;
	slot_id = crc32(key, keylen)%SWARMKV_DEFAULT_SLOT_NUM;
	slot = store->slots[slot_id];
	assert(slot_id == slot.slot_id);
	return slot.owner_node_id;
}

//put
int swarmkv_put(struct swarmkv_store *store, const char *tb_name, const struct swarmkv_writeoptions *options, 
					const char *key, size_t keylen, const char *value, size_t vallen, char **errptr)
{
	int slot_owner_id = -1;
	slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	if(slot_owner_id == store->my_node_id)
	{
		//local put a key-value
		swarmkv_owner_put_kv(store, tb_name, key, keylen, value, vallen);
		swarmkv_msg_send_invalidate_key_req(store, tb_name, key, keylen, value, vallen);
	}
	else
	{
		//base64 encode
		char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
		char *value_base64 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
		base64_encode(key, keylen, key_base64);
		base64_encode(value, vallen, value_base64);

		struct swarmkv_node* peer_node = NULL;
		HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
		if(peer_node == NULL)
		{
			printf("I can't find the owner_node of the key %s!\n", key);
			return -1;
		}
		char *put_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_put_req(store, MSG_PUT_REQ, put_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key_base64, value_base64);
		if(msg_len <= 0)
		{
			printf("Pack put request message error.\n");
			free(put_req_msg);
			put_req_msg = NULL;
			return -1;
		}
		swarmkv_msg_send(store->sockfd, put_req_msg, peer_node->ip, peer_node->port);
		free(put_req_msg);	
		put_req_msg = NULL;
		free(key_base64);
		key_base64 = NULL;
		free(value_base64);
		value_base64 = NULL;
	}
	return 1;
}

int swarmkv_put_with_callback(struct swarmkv_store *store, const char *tb_name, const struct swarmkv_writeoptions *options,
							const char *key, size_t keylen, const char *value, size_t vallen, swarmkv_callback_func_t *cb, void *cb_arg)
{
	int slot_owner_id = -1;
	slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	if(slot_owner_id == store->my_node_id)
	{
		//local put a key-value
		swarmkv_owner_put_kv(store, tb_name, key, keylen, value, vallen);
		swarmkv_msg_send_invalidate_key_req(store, tb_name, key, keylen, value, vallen);
		cb(tb_name, key, keylen, value, vallen, cb_arg);
	}
	else
	{
		//base64 encode
		char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
		char *value_base64 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
		base64_encode(key, keylen, key_base64);
		base64_encode(value, vallen, value_base64);

		struct swarmkv_node* peer_node = NULL;
		HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
		if(peer_node == NULL)
		{
			printf("I can't find the owner_node of the key %s!\n", key);
			return -1;
		}
		char *put_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_put_req_with_cb(store, MSG_PUT_REQ, put_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key_base64, value_base64, (unsigned long int)cb, (unsigned long int)cb_arg);
		if(msg_len <= 0)
		{
			printf("Pack put request message error.\n");
			free(put_req_msg);
			put_req_msg = NULL;
			return -1;
		}
		swarmkv_msg_send(store->sockfd, put_req_msg, peer_node->ip, peer_node->port);
		free(put_req_msg);	
		put_req_msg = NULL;
		free(key_base64);
		key_base64 = NULL;
		free(value_base64);
		value_base64 = NULL;
	}
	return 1;
}

//get a key-value from my store as owner
char *swarmkv_owner_get_kv(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, 
								size_t *vallen, int req_node_id)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	char* value = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I can't find the table!\n");
		return value;
	}
	//HASH_FIND_STR(table->owned_value_hash, key, kv);
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		printf("I can't find the key!\n");
		//free(table);
		//table = NULL;
		return value;
	}
	//insert the request node into UT_array *nodes_have_key_cached
	if(req_node_id != store->my_node_id)
	{
		utarray_push_back(kv->nodes_have_key_cached, &req_node_id);
	}
	vallen = &kv->vallen;
	value = kv->value;
	
	pthread_rwlock_unlock(&store->rwlock);

	return value;
}

//get a key-value from my cache
char *swarmkv_cache_get_kv(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, size_t *vallen)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	char *value = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		printf("I don't have the table in my cache!\n");
		return value;
	}
	//HASH_FIND_STR(table->cached_value_hash, key, kv);
	HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		printf("I don't find the key in my cache!\n");
		//free(table);
		//table = NULL;
		return value;
	}
	pthread_rwlock_unlock(&store->rwlock);
	value = kv->value;
	vallen = &kv->vallen;

	return value;
}


//get keys
//Returend value MUST Be freed by caller.
char *swarmkv_get(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options,
					const char *key, size_t keylen, size_t *vallen, char **errptr)
{
	int slot_owner_id = -1;
	char *value = NULL;
	slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	
	if(slot_owner_id == store->my_node_id)
	{
		//I am the owner of the key
		value = swarmkv_owner_get_kv(store, table, key, keylen, vallen, slot_owner_id);
		//printf("%s: %s\n", key[i], value);
	}
	else
	{
		//check my cache
		value = swarmkv_cache_get_kv(store, table, key, keylen, vallen);
	
		//I haven't cache the kv	
		if(value == NULL)
		{
			//send a get request to owner
			struct swarmkv_node* peer_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
			if(peer_node == NULL)
			{
				printf("I can't find the owner_node of the key %s!\n", key);	
				return value;
			}
			//base64 encode key
			char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			base64_encode(key, keylen, key_base64);

			char *get_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_get_req(store, get_req_msg, SWARMKV_MAX_MSG_SIZE, table, key_base64);
			free(key_base64);
			key_base64 = NULL;
			if(msg_len <= 0)
			{
				printf("Pack get requerst message error.\n");
				return value;
			}
			swarmkv_msg_send(store->sockfd, get_req_msg, peer_node->ip, peer_node->port);
			free(get_req_msg);
			get_req_msg = NULL;
		}
	}
	
	return value;
}

void swarmkv_get_with_callback(struct swarmkv_store *store, const char *tb_name, const struct swarmkv_readoptions *options, 
					const char *key, size_t keylen, swarmkv_callback_func_t *cb, void *cb_arg)
{
	//get from my store or cache
	int slot_owner_id = -1;
	char *value = NULL;
	size_t *vallen = 0;
	slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	
	if(slot_owner_id == store->my_node_id)
	{
		//I am the owner of the key
		value = swarmkv_owner_get_kv(store, tb_name, key, keylen, vallen, slot_owner_id);
		//printf("%s: %s\n", key[i], value);
	}
	else
	{
		//check my cache
		value = swarmkv_cache_get_kv(store, tb_name, key, keylen, vallen);
		//I haven't cache the kv	
		if(value == NULL)
		{
			//send a get request to owner
			struct swarmkv_node* peer_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
			if(peer_node != NULL)
			{
				//base64 encode key
				char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				base64_encode(key, keylen, key_base64);

				char *get_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				size_t msg_len = swarmkv_msg_pack_get_req_with_cb(store, get_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key_base64, (unsigned long int)cb, (unsigned long int)cb_arg);
				free(key_base64);
				key_base64 = NULL;
				if(msg_len > 0)
				{
					swarmkv_msg_send(store->sockfd, get_req_msg, peer_node->ip, peer_node->port);
					free(get_req_msg);
					get_req_msg = NULL;
				}
				else
				{
					printf("Pack get requerst message error.\n");
				}
			}
			else
			{
				printf("I can't find the owner_node of the key %s!\n", key);	
			}	
		}
	}
	if(value != NULL)
	{
		cb(tb_name, key, keylen, value, *vallen, cb_arg);
	}
}

//parse the node list and slot list in ping/pong/meet_resp msg and compare with my store
int swarmkv_parse_node_and_slot(struct swarmkv_store *store, struct swarmkv_health_check health_check_data)
{
	struct swarmkv_node* new_node = NULL;
	int i = 0;
	for (i = 0; i < health_check_data.node_num; i++)
	{	
		HASH_FIND_INT(store->nodes, &health_check_data.nodes[i].node_id, new_node);
		//add new nodes into my store->nodes
		if (new_node == NULL)
		{
			if (health_check_data.nodes[i].status == FAIL)
			{
				continue;
			}
			printf("add a new node.\n");
			new_node = ALLOC(struct swarmkv_node, 1);

			new_node->node_id = health_check_data.nodes[i].node_id;
			new_node->ip = health_check_data.nodes[i].ip;
			new_node->port = health_check_data.nodes[i].port;
			new_node->status = health_check_data.nodes[i].status;
			new_node->ping_sent_t = 0;
			new_node->pong_received_t = 0;
			new_node->pfail_cnt = 0;
			HASH_ADD_INT(store->nodes, node_id, new_node);
		}
		//check the exist node status
		else
		{
			//if the node is failed, update the node status
			if (health_check_data.nodes[i].status == FAIL)
			{
				new_node->status = FAIL;
			}
			//if the node is pfailed, update the node status and increase the pfail_cnt
			if (health_check_data.nodes[i].status == PFAIL)
			{
				new_node->status = PFAIL;
				new_node->pfail_cnt++;
				
				//Check if more than half of the nodes think that the current node is pfail
				int node_cnt = HASH_COUNT(store->nodes);
				if(new_node->pfail_cnt > node_cnt/2)
				{
					//pfail_cnt more than half of nodes, change status to FAIL
					new_node->status = FAIL;
					new_node->pfail_cnt = 0;
					//broadcast a fail msg
					char *fail_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
					size_t msg_len = swarmkv_msg_pack_fail_req(store, fail_req_msg, SWARMKV_MAX_MSG_SIZE, health_check_data.nodes[i].node_id);
					//swarmkv_msg_pack_fail_req(store, id);
					if(msg_len <= 0)
					{
						printf("Pack fail request message error.\n");
						return -1;
					}
					struct swarmkv_node *node = NULL;
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
	for (i = 0; i < health_check_data.slot_num; i++)
	{
		int slot_id = health_check_data.slots[i].slot_id;
		if(health_check_data.slots[i].owner_node_id != store->slots[slot_id].owner_node_id)
		{
			store->slots[slot_id].owner_node_id = health_check_data.slots[i].owner_node_id;
		}
	}
	return 1;
}

int caculate_slot_table(struct swarmkv_store *store, struct swarmkv_slot *new_slots)
{
	int node_num = HASH_COUNT(store->nodes);
	int point_num = node_num * SWARMKV_POINTS_PER_NODE_ON_CHR;
	struct conhash* ch=NULL;
	ch = swarmkv_conhash_create(store->nodes, SWARMKV_POINTS_PER_NODE_ON_CHR);
	int i = 0;
	int j = 0;
	for(i=0; i<point_num; i++)
	{
		if( i == 0)
		{
			for(j = 0; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
			for(j=ch->point[point_num-1].point_val+1; j<SWARMKV_DEFAULT_SLOT_NUM; j++)
			{
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
		else
		{
			for(j = ch->point[i-1].point_val+1; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
	}
	free(ch);
	ch = NULL;
	return 1;
}

int swarmkv_msg_unpack(struct swarmkv_store* store, char *buf)
{
	//parse cjson message to swarmkv_msg struct
	struct swarmkv_msg *msg = ALLOC(struct swarmkv_msg, 1);
    assert(swarmkv_js_msg_unpack(buf, msg) > 0);
	
	size_t msg_len = -1;
	struct swarmkv_node *src_node = NULL;
	HASH_FIND_INT(store->nodes, &msg->src_node_id, src_node); 
	//check if the src node is legal
	//except meet messages, other messages from a unknown node are illegal
	if(src_node == NULL)
	{
		if((msg->type != MSG_MEET_RESP) && (msg->type != MSG_MEET_REQ))
		{
			
			printf("It is a message from an UNKNOWN node %d!\n", msg->src_node_id);
			return -1;
		}
	}
	char *key = NULL;
	char *value = NULL;
	size_t keylen = -1;
	size_t vallen = -1;
	struct swarmkv_table* table = NULL;
	struct swarmkv_key_value* kv = NULL;
	struct swarmkv_migrate_kv mkv[SWARMKV_MAX_MIGRATE_KV_NUM];
	int i = 0;
	int slot_owner_id = -1;

	switch(msg->type)
	{
		case MSG_PING:
		case MSG_PONG:
			if(msg->type == MSG_PING)
			{
				printf("It is a PING msg\n");
			}
			else if((msg->type == MSG_PONG))
			{
				src_node->pong_received_t = clock();
				src_node->ping_sent_t = 0;
				printf("It is a PONG msg\n");
			}

			//update the status of src_node to ON
			if(src_node->status != ON)
			{
				src_node->status = ON;
				src_node->pfail_cnt = 0;
			}
			//parse and compare nodes and slots infomation with my store
			swarmkv_parse_node_and_slot(store, msg->data.health_check_data);

			//send a PONG response
			
			if(msg->type == MSG_PING)
			{
				char *resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				msg_len = swarmkv_msg_pack_health_check(store, MSG_PONG, resp_msg, SWARMKV_MAX_MSG_SIZE);
				if(msg_len <= 0)
				{
					printf("Pack pong message error\n");
					return -1;
				}
				swarmkv_msg_send(store->sockfd, resp_msg, src_node->ip, src_node->port);
				free(resp_msg);
				resp_msg = NULL;
			}

			//printf("Press enter to continue...\n");
			//getchar();
			break;

		case MSG_MEET_REQ:
			printf("it is a MEET REQUEST msg\n");
			if(src_node == NULL)
			{
				printf("Meet a new node\n");
				src_node = ALLOC(struct swarmkv_node, 1);
				src_node->node_id = msg->src_node_id;
				HASH_ADD_INT(store->nodes, node_id, src_node);
			}
			else
			{
				printf("Meet a exist failed node\n");
			}
			printf("%s\n", msg->src_ip);
			src_node->ip = ALLOC(char, strlen(msg->src_ip));
			memcpy(src_node->ip, msg->src_ip, strlen(msg->src_ip));
			//src_node->ip = msg->src_ip;
			src_node->port = msg->src_port;
			src_node->ping_sent_t = 0;
			src_node->pong_received_t = 0;
			src_node->status = ON;
			src_node->pfail_cnt = 0;
		
			//send a meet response msg
			printf("src_node->ip: %s\n", src_node->ip);
			char *meet_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			msg_len = swarmkv_msg_pack_health_check(store, MSG_MEET_RESP, meet_resp_msg, SWARMKV_MAX_MSG_SIZE);
			printf("msg_len: %ld\n",msg_len);
			if(msg_len <= 0)
			{
				printf("Pack meet response error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, meet_resp_msg, src_node->ip, src_node->port);
			free(meet_resp_msg);
			meet_resp_msg = NULL;

			//printf("Press enter to continue...\n");
			//getchar();

			break;

		case MSG_MEET_RESP:
			printf("It is a MEET RESPONSE msg\n");
			
			if(src_node == NULL)
			{
				src_node = ALLOC(struct swarmkv_node, 1);
				src_node->node_id = msg->src_node_id;
				HASH_ADD_INT(store->nodes, node_id, src_node);
			}
			src_node->ip = msg->src_ip;
			src_node->port = msg->src_port;
			src_node->ping_sent_t = 0;
			src_node->pong_received_t = 0;
			src_node->status = ON;
			src_node->pfail_cnt = 0;

			swarmkv_parse_node_and_slot(store, msg->data.health_check_data);
			//caculate slot table
			struct swarmkv_slot *new_slots = ALLOC(struct swarmkv_slot, SWARMKV_DEFAULT_SLOT_NUM);
			caculate_slot_table(store, new_slots);
			
			for(i=0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
			{
				if(new_slots[i].owner_node_id == store->my_node_id)
				{
					//send migreate key request
					struct swarmkv_node *curr_node = NULL;
					HASH_FIND_INT(store->nodes, &store->slots[i].owner_node_id, curr_node);
					if(curr_node == NULL)
					{
						printf("I can't find the current owner node of the slot %d", i);
						continue;
					}
					char *migrate_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
					msg_len = swarmkv_msg_pack_migration_req(store, migrate_req_msg, SWARMKV_MAX_MSG_SIZE, new_slots[i].slot_id);
					if(msg_len<=0)
					{
						printf("Pack fail response message error.\n");
						return -1;
					}
					swarmkv_msg_send(store->sockfd, migrate_req_msg, curr_node->ip, curr_node->port);
					free(migrate_req_msg);
					migrate_req_msg = NULL;
				}
			}
			//printf("Press enter to continue...\n");
			//getchar();
			break;

		case MSG_FAIL_REQ:
			printf("It is a FAIL msg\n");
			//find the fail node
			struct swarmkv_node *fail_node = NULL;
			int fail_node_id = msg->data.fail_req_data.failed_node_id;
			HASH_FIND_INT(store->nodes, &fail_node_id, fail_node); 
			if(fail_node != NULL)
			{
				fail_node->status = FAIL;
				fail_node->pfail_cnt = 0;
				//send a fail response msg
				char *fail_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				msg_len = swarmkv_msg_pack_fail_response(store, fail_resp_msg, SWARMKV_MAX_MSG_SIZE, fail_node_id, OP_SUCCESS);
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
			//decode key and value
			key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			keylen = base64_decode(msg->data.put_req_data.key, strlen(msg->data.put_req_data.key), key);
			//check slot is belong to me.
			slot_owner_id = caculate_key_slot_owner(store, key, keylen);
			char *put_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			if(slot_owner_id != store->my_node_id)
			{
				if(msg->ttl > 0)
				{
					struct swarmkv_node *own_node = NULL;
					HASH_FIND_INT(store->nodes, &slot_owner_id, own_node);
					if(own_node == NULL)
					{
						printf("put error.\n");
						return -1;
					}
					msg_len = swarmkv_msg_repack_ttl(buf, put_resp_msg, SWARMKV_MAX_MSG_SIZE);
					swarmkv_msg_send(store->sockfd, buf, own_node->ip, own_node->port);
					break;
				}
				else
				{
					//返回REQ_ERROR消息
					msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
																msg->data.put_req_data.key, msg->data.put_req_data.value, REQ_ERROR);
				}
			}
			else
			{
				value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				vallen = base64_decode(msg->data.put_req_data.value, strlen(msg->data.put_req_data.value), value);
				//put the key-value into my store and send invalidate request msg
				swarmkv_owner_put_kv(store, msg->data.put_req_data.table_name, key, keylen, value, vallen);
				swarmkv_msg_send_invalidate_key_req(store, msg->data.put_req_data.table_name, key, keylen, value, vallen);

				//send a put response
				if(msg->data.get_req_data.cb_function<0)
				{
					msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
																	msg->data.put_req_data.key, msg->data.put_req_data.value, OP_SUCCESS);
				}
				else
				{
					msg_len = swarmkv_msg_pack_put_resp_with_cb(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
																	msg->data.put_req_data.key, msg->data.put_req_data.value, OP_SUCCESS, msg->src_uuid, 
																	msg->data.get_req_data.cb_function, msg->data.get_req_data.cb_arg);
				}
			}
			if(msg_len <= 0)
			{
				printf("Pack put response message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, put_resp_msg, src_node->ip, src_node->port);
			free(put_resp_msg);
			put_resp_msg = NULL;
			break;

		case MSG_PUT_RESP:
			//callback if any
			if(msg->data.put_resp_data.op_result == OP_SUCCESS)
			{
				if(msg->data.put_resp_data.uuid_version == store->uuid)
				{
					swarmkv_callback_func_t *cb_func = (swarmkv_callback_func_t*)msg->data.put_resp_data.cb_function;
					void *cb_arg = (void*)((unsigned long int)msg->data.put_resp_data.cb_arg);
					cb_func(table->table_name, key, keylen, value, vallen, cb_arg);
				}
			}
			else if(msg->data.put_resp_data.op_result == REQ_ERROR)
			{
				printf("The key owner you requested is wrong.\n");
			}
			break;
		
		case MSG_INVALIDATE_KEYS_REQ:
			//update the cached_value_hash
			pthread_rwlock_wrlock(&store->rwlock);
			HASH_FIND_STR(store->table_hash, msg->data.invalidate_req_data.table_name, table);
			if(table != NULL)
			{
				//decode key and value
				key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				keylen = base64_decode(msg->data.invalidate_req_data.key, strlen(msg->data.invalidate_req_data.key), key);
				vallen = base64_decode(msg->data.invalidate_req_data.value, strlen(msg->data.invalidate_req_data.value), value);

				//HASH_FIND_STR(table->cached_value_hash, key->valuestring, cache_kv);
				HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
				if(kv != NULL)
				{
					kv->value = value;
					kv->vallen = vallen;
					//send a invalidate key response "ok"
					char *invalidate_key_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
					msg_len = swarmkv_msg_pack_put_resp(store, MSG_INVALIDATE_KEYS_RESP, invalidate_key_resp_msg, SWARMKV_MAX_MSG_SIZE, 
												msg->data.invalidate_req_data.table_name, msg->data.invalidate_req_data.key, msg->data.invalidate_req_data.value, OP_SUCCESS);
					if(msg_len <= 0)
					{
						printf("Pack put invalidate response message error.\n");
						pthread_rwlock_unlock(&store->rwlock);
						break;
					}
					swarmkv_msg_send(store->sockfd, invalidate_key_resp_msg, src_node->ip, src_node->port);
					free(invalidate_key_resp_msg);
					invalidate_key_resp_msg = NULL;
				}
			}
			pthread_rwlock_unlock(&store->rwlock);
			break;

		case MSG_INVALIDATE_KEYS_RESP:
			break;
		
		case MSG_GET_REQ:
			//decode key
			key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			keylen = base64_decode(msg->data.get_req_data.key, strlen(msg->data.get_req_data.key), key);
			//check the key is belong to me?
			slot_owner_id = caculate_key_slot_owner(store, key, keylen);
			char *get_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			if(slot_owner_id != store->my_node_id)
			{
				if(msg->ttl > 0)
				{
					struct swarmkv_node *own_node = NULL;
					HASH_FIND_INT(store->nodes, &slot_owner_id, own_node);
					if(own_node == NULL)
					{
						printf("put error.\n");
						return -1;
					}
					msg_len = swarmkv_msg_repack_ttl(buf, get_resp_msg, SWARMKV_MAX_MSG_SIZE);
					//Forward to the node which I think the slot is belong to.
					swarmkv_msg_send(store->sockfd, get_resp_msg, own_node->ip, own_node->port);
					break;
				}
				else
				{
					swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, 
												msg->data.get_req_data.key, NULL, REQ_ERROR);
				}
			}
			size_t *value_len = NULL;
			value_len = ALLOC(size_t, 1);
			value = swarmkv_owner_get_kv(store, msg->data.get_req_data.table_name, key, keylen, value_len, src_node->node_id);
			if(value == NULL)
			{
				printf("get key %s from my store for node %d error.\n", key, src_node->node_id);
				break;
			}
			//encode value
			char *value_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			base64_encode(value, *value_len, value_base64);
			if(msg->data.get_req_data.cb_function<0)
			{
				msg_len = swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, msg->data.get_req_data.key, value_base64, OP_SUCCESS);
			}
			else
			{
				msg_len = swarmkv_msg_pack_get_resp_with_cb(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, msg->data.get_req_data.key, 
									value_base64, OP_SUCCESS, msg->src_uuid, msg->data.get_req_data.cb_function, msg->data.get_req_data.cb_arg);
			}
			if(msg_len <= 0 )
			{
				printf("pack a get response message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, get_resp_msg, src_node->ip, src_node->port);
			free(get_resp_msg);
			get_resp_msg = NULL;
			free(value_len);
			value_len = NULL;
			
			break;

		case MSG_GET_RESP:
			//decode key and value
			key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			keylen = base64_decode(msg->data.get_resp_data.key, strlen(msg->data.get_resp_data.key), key);
			vallen = base64_decode(msg->data.get_resp_data.value, strlen(msg->data.get_resp_data.value), value);
			
			//update my cache	
			pthread_rwlock_wrlock(&store->rwlock);
			HASH_FIND_STR(store->table_hash, msg->data.get_resp_data.table_name, table);
			if(table == NULL)
			{
				//add a new table in my swarmkv_store
				table = ALLOC(struct swarmkv_table, 1);
				memcpy(table->table_name, msg->data.get_resp_data.table_name, strlen(msg->data.get_resp_data.table_name));
				HASH_ADD_STR(store->table_hash, table_name, table);
				//add a new key
				kv = ALLOC(struct swarmkv_key_value, 1);
				kv->key = key;
				//HASH_ADD_STR(tb->cached_value_hash, key, kv);
				HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
			}
			//HASH_FIND_STR(tb->cached_value_hash, key, kv);
			HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
			if(kv == NULL)
			{
				//add a new key
				kv = ALLOC(struct swarmkv_key_value, 1);
				kv->key = key;
				HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
			}
			kv->keylen = keylen;
			kv->value = value;
			kv->vallen = vallen;
			pthread_rwlock_unlock(&store->rwlock);
			//callback
			if(msg->data.get_resp_data.cb_function > 0)
			{
				if(msg->data.get_resp_data.uuid_version == store->uuid)
				{
					swarmkv_callback_func_t *cb_func = (swarmkv_callback_func_t*)msg->data.get_resp_data.cb_function;
					void *cb_arg = (void*)((unsigned long int)msg->data.get_resp_data.cb_arg);
					cb_func(table->table_name, key, keylen, value, vallen, cb_arg);
				}
			}
			break;

		case MSG_MIGRATE_REQ:
			i = 0;
			pthread_rwlock_wrlock(&store->rwlock);
			for(table=store->table_hash; table!=NULL; table=(struct swarmkv_table *)(table->hh.next))
			{
				struct swarmkv_key_value *owned_kv = NULL;
				for(owned_kv=table->owned_value_hash; owned_kv!=NULL; owned_kv=(struct swarmkv_key_value *)(owned_kv->hh.next))
				{
					int slot_id = caculate_key_slot_owner(store, owned_kv->key, owned_kv->keylen);
					if(msg->data.migrate_req_data.slot_id == slot_id)
					{
						//base64 encode key and value
						key = ALLOC(char, owned_kv->keylen*2);
						value = ALLOC(char, owned_kv->vallen*2);
						base64_encode(owned_kv->key, owned_kv->keylen, key);
						base64_encode(owned_kv->value, owned_kv->vallen, value);
						mkv[i].key = key;
						mkv[i].value = value;
						mkv[i].table_name = table->table_name;
						i++;
					}
				}
			}
			//change the slot owner on my store->slots
			store->slots[msg->data.migrate_req_data.slot_id].owner_node_id = src_node->node_id;
			//send a migrate response
			char *migrate_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			msg_len = swarmkv_msg_pack_migration_resp(store, migrate_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.migrate_req_data.slot_id, mkv, i);
			if(msg_len <= 0)
			{
				printf("pack a migrate response message error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, get_resp_msg, src_node->ip, src_node->port);
			pthread_rwlock_unlock(&store->rwlock);
			free(migrate_resp_msg);
			migrate_resp_msg = NULL;
			break;

		case MSG_MIGRATE_RESP:
			pthread_rwlock_wrlock(&store->rwlock);
			for(i=0; i<msg->data.migrate_resp_data.kv_num; i++)
			{
				//decode key and value
				key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				keylen = base64_decode(msg->data.migrate_resp_data.mkv[i].key, strlen(msg->data.migrate_resp_data.mkv[i].key), key);
				vallen = base64_decode(msg->data.migrate_resp_data.mkv[i].value, strlen(msg->data.migrate_resp_data.mkv[i].value), value);

				HASH_FIND_STR(store->table_hash, msg->data.migrate_resp_data.mkv[i].table_name, table);
				if(table == NULL)
				{
					table = ALLOC(struct swarmkv_table, 1);
					memcpy(table->table_name, msg->data.get_resp_data.table_name, strlen(msg->data.migrate_resp_data.mkv[i].table_name));
					HASH_ADD_STR(store->table_hash, table_name, table);
					//add a new key
					kv = ALLOC(struct swarmkv_key_value, 1);
					kv->key = key;
					HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
				}
				HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
				if(kv == NULL)
				{
					//add a new key
					kv = ALLOC(struct swarmkv_key_value, 1);
					kv->key = key;
					HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
				}
				kv->keylen = keylen;
				kv->value = value;
				kv->vallen = vallen;
			}
			store->slots[msg->data.migrate_resp_data.slot_id].owner_node_id = store->my_node_id;
			pthread_rwlock_unlock(&store->rwlock);
			break;
		default:
			printf("UNKNOWN msg\n");
	}
	free(msg);
	msg = NULL;
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	return 1;
}

//select a node: has minimal pong_received_t
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
	struct swarmkv_node *candidate_node = NULL;
	for (i = 0; i < choose_in; i++)
	{
		int node_id = rand()%nodes_cnt;
		if(node_id == store->my_node_id)
		{
			continue;
		}
		HASH_FIND_INT(store->nodes, &node_id, candidate_node);
		if(candidate_node == NULL)
		{
			continue;
		}
		if(candidate_node->ping_sent_t != 0)
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

//ping the nodes: never ping before (i.e. pong_received_t=0, never receive a pong) or cur_t - pong_received_t > TIMEOUT/2
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
		//if (node->pong_received_t == 0 || cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT / 2)
		if (cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT / 2)
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
			swarmkv_msg_unpack(store, recvbuf);	
		}
		
		clock_t cur_t = clock();
		//Traverse all nodes per 100ms
		if(cur_t/SWARMKV_PING_INTERVAL >= check_cnt)
		{
			check_cnt++;
			swarmkv_regular_ping(store, cur_t);
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

//prase config and bootstraps infomation set by user
int swarmkv_config_and_init(struct swarmkv_store *store, const char *bootstraps, const char *config)
{
	struct bootstrap_node *bs_nodes;

	int bs_info_len = strlen(bootstraps);
	char *bootstrap_info = ALLOC(char, bs_info_len);
	memcpy(bootstrap_info, bootstraps, bs_info_len);

	int config_info_len = strlen(config);
	char *config_info = ALLOC(char, config_info_len);
	memcpy(config_info, config, config_info_len);
	
	char *my_id_info = NULL;
	char *my_info = NULL;
	char *peer_node_info = NULL;
    char *p = NULL;
	char *p1 = NULL;
	char *peer_ip = NULL;
	uint16_t peer_port;

	my_id_info = strtok_r(config_info, ";", &p);
	if(my_id_info == NULL)
	{
		printf("Please set your node id. Format: node_id=id;token=xxxxx,db=0\n");
		free(bootstrap_info);
		bootstrap_info = NULL;
		free(config_info);
		config_info = NULL;
		return -1;
	}
	strtok_r(my_id_info, "=", &p);
	store->my_node_id = atoi(strtok_r(NULL, "=", &p));
	printf("%d\n", store->my_node_id);

	my_info = strtok_r(bootstrap_info, ";", &p);
	strtok_r(my_info, "=", &p1);
	my_info = strtok_r(NULL, "=", &p1);
	printf("my_info: %s\n",my_info);
	peer_node_info = strtok_r(NULL, ";", &p);
	strtok_r(peer_node_info, "=", &p1);
	peer_node_info = strtok_r(NULL, ";", &p1); 
	printf("peer_node_info: %s\n",peer_node_info);

    // my_info example 172.16.224.160:8323
	if(my_info == NULL || peer_node_info == NULL)
	{
		free(bootstrap_info);
		bootstrap_info = NULL;
		free(config_info);
		config_info = NULL;
		printf("Please set your ip&port and bootstrap_nodes ip&port. Format: self=ip:port;peers=ip0:port0,ip1:port1,...\n");
		return -1;
	}
	
	char* my_ip = strtok_r(my_info, ":", &p);
	int ip_len = strlen(my_ip);
	store->my_ip = ALLOC(char, ip_len);
	memcpy(store->my_ip, my_ip, ip_len);
	
	store->my_port = atoi(strtok_r(NULL, ":", &p));
	//printf("store->my_ip: %s\n", store->my_ip);
	//printf("store->my_port: %d\n", store->my_port);

	//create socket
	store->sockfd = swarmkv_create_socket(store->my_port);
	printf("store->sockfd : %d\n", store->sockfd);

	//parse bootstrap nodes and send meet requests
	char *peer_nodes;
	int meet_cnt = 0;
	// peer_node_info example 172.16.224.160:6323,172.16.224.160:7323,172.16.224.160:18323
	peer_node_info = strtok_r(peer_node_info, ",", &p);
	
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
			continue;
		}
		swarmkv_msg_send(store->sockfd, meet_req, peer_ip, peer_port);
		free(meet_req);
		meet_req = NULL;
		meet_cnt ++;
		peer_node_info = strtok_r(NULL, ",", &p);
	}
	
	free(bootstrap_info);
	bootstrap_info = NULL;
	free(config_info);
	config_info = NULL;
	printf("my_ip: %s\n", store->my_ip);

	if(meet_cnt == 0)
	{
		printf("error: None of the meet messages were sent successfully.\n");
		return -1;
	}
	return 1;
}

struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err)
{
	//parse my_node_id from config
	//parse my port from bootstraps
	struct swarmkv_store *store = NULL;
	store = ALLOC(struct swarmkv_store, 1);
	
	uuid_t uuid;
    uuid_generate(store->uuid); 
	uuid_unparse(uuid, store->uuid);
	
	//init, set my info(ip.port,id), parse bootstrap node information and send meet msg to them
	if(swarmkv_config_and_init(store, bootstraps, config) < 0)
	{
		printf("Startup failed, please reconfigure.\n");
		return NULL;
	}

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

	//init slots[SWARMKV_DEFAULT_SLOT_NUM], all slot is belong to me
	int i = 0;
	for(i = 0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
	{
		store->slots[i].slot_id = i;
		store->slots[i].owner_node_id = store->my_node_id;
	}
	
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

