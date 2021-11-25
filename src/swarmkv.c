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
#include "swarmkv_message.h"
#include "uthash/uthash.h"
#include "uthash/utarray.h"
#include "swarmkv_base64.h"
#include "swarmkv_crc32.h"
#include "swarmkv_conhash.h"

#define BUFSIZE 65500
#define SWARMKV_DEFAULT_LISTEN_PORT 8323
#define SWARMKV_MAX_KEY_LEN 512
#define SWARMKV_MAX_VAL_LEN 2048
#define SWARMKV_ONCE_MIGRATE_KV_NUM 20

#define SWARMKV_DEFAULT_NODE_TIMEOUT_US 15000000    //15s
#define SWARMKV_REGULAR_PING_INTERVAL_US 100000   //100ms
#define SWARMKV_RANDOM_PING_INTERVAL_US 1000000   //1s

static int collision_cnt;

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
	}
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = ALLOC(char, keylen);
		memcpy(kv->key, key, keylen);
		HASH_ADD_KEYPTR(hh, table->owned_value_hash, kv->key, keylen, kv);
		utarray_new(kv->nodes_have_key_cached,&ut_int_icd);

		kv->keylen = keylen;
		kv->value = ALLOC(char, vallen);
		memcpy(kv->value, value, vallen);
		kv->vallen = vallen;
	}	
	else
	{
		if(strcmp(kv->key, key)!=0)
		{
			collision_cnt++;
			printf("collision\n");
		}
		else
		{
			free(kv->value);
			kv->value = NULL;
			kv->value = ALLOC(char, vallen);
			memcpy(kv->value, value, vallen);
			kv->vallen = vallen;
		}
	}
	/*
	printf("check if put success\n");
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv!=NULL)
	{
		printf("%s:%s\n",kv->key, kv->value);
	}
	*/
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
	HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		printf("I don't have the key %s in the table %s\n", key, tb_name);
		return -1;
	}
	
	if(kv->nodes_have_key_cached!=NULL)
	{
		//base64 encode key and value
		char *key_base64 = ALLOC(char, keylen*2);
		char *value_base64 = ALLOC(char, vallen*2);
		base64_encode(key, keylen, key_base64);
		base64_encode(value, vallen, value_base64);

		int* cached_node_id = NULL;
		struct swarmkv_node* cache_node = NULL;
		for(cached_node_id=(int*)utarray_front(kv->nodes_have_key_cached); cached_node_id!= NULL; cached_node_id=(int*)utarray_next(kv->nodes_have_key_cached,cached_node_id)) 
		{
			HASH_FIND_INT(store->nodes, cached_node_id, cache_node);
			if(cache_node != NULL)
			{
				char *invalidate_key_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				size_t msg_len = swarmkv_msg_pack_put_req(store, MSG_INVALIDATE_KEYS_REQ, invalidate_key_req_msg, SWARMKV_MAX_MSG_SIZE, tb_name, key_base64, value_base64);
				assert(msg_len > 0);
				swarmkv_msg_send(store->sockfd, invalidate_key_req_msg, msg_len, cache_node->ip, cache_node->port);
				free(invalidate_key_req_msg);
				invalidate_key_req_msg = NULL;
			}
		}
		free(key_base64);
		key_base64 = NULL;
		free(value_base64);
		value_base64 = NULL;
	}
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

//caculate the slot id of key
int caculate_key_slot_id(struct swarmkv_store *store, const char *key, size_t keylen)
{
	struct swarmkv_slot slot;
	int slot_id = -1;
	slot_id = crc32(key, keylen)%SWARMKV_DEFAULT_SLOT_NUM;
	slot = store->slots[slot_id];
	assert(slot_id == slot.slot_id);
	//printf("***slot_id:%d\n",slot.slot_id);
	return slot.slot_id;
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
		printf("put kv in my store\n");
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
		assert(msg_len > 0);
		swarmkv_msg_send(store->sockfd, put_req_msg, msg_len, peer_node->ip, peer_node->port);
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
		assert(msg_len > 0);
		swarmkv_msg_send(store->sockfd, put_req_msg, msg_len, peer_node->ip, peer_node->port);
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
	//这个value是返回值，不用在该函数内释放内存，但是外面接收返回值的函数要记得释放
	char* value = NULL;         
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		printf("I can't find the table!\n");	
	}
	else
	{
		HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
		if(kv == NULL)
		{
			printf("I can't find the key!\n");
		}
		else
		{
			printf("I find the key!\n");
			value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
			memcpy(value, kv->value, kv->vallen);
			*vallen = kv->vallen;
			//insert the request node into UT_array *nodes_have_key_cached
			if(req_node_id != store->my_node_id)
			{
				int req_node = req_node_id;
				utarray_push_back(kv->nodes_have_key_cached, &req_node);
			}
		}
	}
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
	}
	else
	{
		HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
		if(kv == NULL)
		{
			printf("I don't find the key in my cache!\n");
		}
		else
		{
			value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
			memcpy(value, kv->value, kv->vallen);
			*vallen = kv->vallen;
		}
	}
	pthread_rwlock_unlock(&store->rwlock);
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
		//printf("check the vallen: %ld\n", (*vallen));
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
			}
			else
			{
				//base64 encode key
				char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				base64_encode(key, keylen, key_base64);

				char *get_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				size_t msg_len = swarmkv_msg_pack_get_req(store, get_req_msg, SWARMKV_MAX_MSG_SIZE, table, key_base64);
				assert(msg_len > 0);
				free(key_base64);
				key_base64 = NULL;
				swarmkv_msg_send(store->sockfd, get_req_msg, msg_len, peer_node->ip, peer_node->port);
				free(get_req_msg);
				get_req_msg = NULL;
			}
		}
	}
	if(value==NULL)
	{
		printf("find null\n");
	}
	return value;
}

void swarmkv_get_with_callback(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options, 
					const char *key, size_t keylen, swarmkv_callback_func_t *cb, void *cb_arg)
{
	printf("start get with callback\n");
	int slot_owner_id = -1;
	char *value = NULL;
	size_t *vallen = ALLOC(size_t, 1);
	slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	
	if(slot_owner_id == store->my_node_id)
	{	
		value = swarmkv_owner_get_kv(store, table, key, keylen, vallen, slot_owner_id);
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
			}
			else
			{
				//base64 encode key
				char *key_base64 = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				base64_encode(key, keylen, key_base64);

				char *get_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
				printf("callback address: %ld\n", (unsigned long int)cb);
				size_t msg_len = swarmkv_msg_pack_get_req_with_cb(store, get_req_msg, SWARMKV_MAX_MSG_SIZE, table, key_base64, (unsigned long int)cb, (unsigned long int)cb_arg);
				assert(msg_len > 0);
				free(key_base64);
				key_base64 = NULL;
				swarmkv_msg_send(store->sockfd, get_req_msg, msg_len, peer_node->ip, peer_node->port);
				free(get_req_msg);
				get_req_msg = NULL;
			}	
		}
	}
	if(value != NULL)
	{
		printf("start callback\n");
		cb(table, key, keylen, value, *vallen, cb_arg);
	}
}

//parse the node list and slot list in ping/pong/meet_resp msg and compare with my store
//把meet的node和slot处理单独拿出来再写一个函数，因为meet可以全部接收，ping和pong要比较一下是不是比自己新的版本
int swarmkv_parse_node_and_slot(struct swarmkv_store *store, struct swarmkv_basic_infomation swarmkv_basic_info)
{
	struct swarmkv_node* new_node = NULL;
	pthread_rwlock_wrlock(&store->rwlock);
	int i = 0;
	
	for (i = 0; i < swarmkv_basic_info.node_num; i++)
	{	
		HASH_FIND_INT(store->nodes, &swarmkv_basic_info.nodes[i].node_id, new_node);
		//add new nodes into my store->nodes
		if (new_node == NULL)
		{
			if (swarmkv_basic_info.nodes[i].status == FAIL)
			{
				continue;
			}
			printf("add a new node.\n");
			new_node = ALLOC(struct swarmkv_node, 1);
			new_node->node_id = swarmkv_basic_info.nodes[i].node_id;
			new_node->ip = swarmkv_basic_info.nodes[i].ip;
			new_node->port = swarmkv_basic_info.nodes[i].port;
			new_node->status = swarmkv_basic_info.nodes[i].status;
			new_node->ping_sent_time = 0;
			new_node->pong_received_time = 0;
			new_node->pfail_cnt = 0;
			HASH_ADD_INT(store->nodes, node_id, new_node);
		}
		//check the exist node status
		else
		{
			/*
			//if the node is failed, update the node status
			//是不是应该只根据专门的fail消息来转变fail状态
			if (swarmkv_basic_info.nodes[i].status == FAIL)
			{
				new_node->status = FAIL;
			}
			*/
			//如果我认为这个节点是pfailed，同时当前ping我的节点也认为这个节点是pfailed，计数加一，
			//因为我们不能通过gossip去要求其他节点接受pfail的状态，这是一个主观状态，只能统计其他节点是不是自己也是认为当前节点可能下线了
			if ((swarmkv_basic_info.nodes[i].status) == PFAIL && (new_node->status = PFAIL))
			{
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
					size_t msg_len = swarmkv_msg_pack_fail_req(store, fail_req_msg, SWARMKV_MAX_MSG_SIZE, swarmkv_basic_info.nodes[i].node_id);
					//swarmkv_msg_pack_fail_req(store, id);
					assert(msg_len > 0);
					struct swarmkv_node *node = NULL;
					for(node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) 
					{
						swarmkv_msg_send(store->sockfd, fail_req_msg, msg_len, node->ip, node->port);
					}
					free(fail_req_msg);
					fail_req_msg = NULL;
				}			
			}
		}
	}
	
	for (i = 0; i < swarmkv_basic_info.slot_num; i++)
	{
		int slot_id = swarmkv_basic_info.slots[i].slot_id;
		if(swarmkv_basic_info.slots[i].owner_node_id != store->slots[slot_id].owner_node_id)
		{
			if(store->slots[slot_id].owner_node_id == store->my_node_id)
			{
				continue;    //自己的所有权会在迁移完成后主动放弃，不能因为收到对方的ping/pong而更改自己的持有权
			}
			store->slots[slot_id].owner_node_id = swarmkv_basic_info.slots[i].owner_node_id;
		}
	}
	
	pthread_rwlock_unlock(&store->rwlock);
	return 1;
}

int swarmkv_init_node_and_slot(struct swarmkv_store *store, struct swarmkv_basic_infomation swarmkv_basic_info)
{
	struct swarmkv_node* new_node = NULL;
	pthread_rwlock_wrlock(&store->rwlock);
	int i = 0;

	for (i = 0; i < swarmkv_basic_info.node_num; i++)
	{	
		HASH_FIND_INT(store->nodes, &swarmkv_basic_info.nodes[i].node_id, new_node);
		//add new nodes into my store->nodes
		if (new_node == NULL)
		{
			if (swarmkv_basic_info.nodes[i].status == FAIL)
			{
				continue;
			}
			printf("add a new node.\n");
			new_node = ALLOC(struct swarmkv_node, 1);
			new_node->node_id = swarmkv_basic_info.nodes[i].node_id;
			new_node->ip = swarmkv_basic_info.nodes[i].ip;
			new_node->port = swarmkv_basic_info.nodes[i].port;
			new_node->status = swarmkv_basic_info.nodes[i].status;
			new_node->ping_sent_time = 0;
			new_node->pong_received_time = 0;
			new_node->pfail_cnt = 0;
			HASH_ADD_INT(store->nodes, node_id, new_node);
		}
	}
	for (i = 0; i < swarmkv_basic_info.slot_num; i++)
	{
		int slot_id = swarmkv_basic_info.slots[i].slot_id;
		if(swarmkv_basic_info.slots[i].owner_node_id != store->slots[slot_id].owner_node_id)
		{
			store->slots[slot_id].owner_node_id = swarmkv_basic_info.slots[i].owner_node_id;
		}
	}
	pthread_rwlock_unlock(&store->rwlock);
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
		//printf("%d ", ch->point[i].point_val);
		if( i == 0)
		{
			for(j = 0; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
			for(j=ch->point[point_num-1].point_val+1; j<SWARMKV_DEFAULT_SLOT_NUM; j++)
			{
				new_slots[j].slot_id = j;
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
		else
		{
			for(j = ch->point[i-1].point_val+1; j<=ch->point[i].point_val; j++)
			{
				new_slots[j].slot_id = j;
				//printf("%d ", new_slots[j].slot_id);
				new_slots[j].owner_node_id = ch->point[i].node_id;
			}
		}
	}
	free(ch);
	ch = NULL;
	return 1;
}

static struct msg_count msg_cnt;
void msg_statistics(int msg_type)
{
	msg_cnt.total_cnt++;
	switch(msg_type)
	{
		case MSG_PING:
			printf("It is a PING msg\n");
			msg_cnt.ping_cnt++;
			break;
		case MSG_PONG:
			printf("It is a PONG msg\n");
			msg_cnt.pong_cnt++;
			break;
		case MSG_MEET_REQ:
			printf("It is a MEET REQUEST msg\n");
			msg_cnt.meet_req_cnt++;
			break;
		case MSG_MEET_RESP:  
			printf("It is a MEET RESPONSE msg\n");
			msg_cnt.meet_resp_cnt++;
			break;
		case MSG_FAIL_REQ:
			printf("It is a FAIL REQUEST msg\n");
			msg_cnt.fail_req_cnt++;
			break;
		case MSG_FAIL_RESP:
			printf("It is a FAIL RESPONSE msg\n");
			msg_cnt.fail_resp_cnt++;
			break;
		case MSG_MIGRATE_REQ:
			printf("It is a MIGRATE REQUEST msg\n");
			msg_cnt.migrate_req_cnt++;
			break;
		case MSG_MIGRATE_RESP:
			printf("It is a MIGRATE RESPONSE msg\n");
			msg_cnt.migrate_resp_cnt++;
			break;
		case MSG_DEL_KV:
			printf("It is a DEL KEY-VALUE msg\n");
			msg_cnt.del_kv_cnt++;
			break;
		case MSG_SLOT_OWNER_CHANGE:
			printf("It is a SLOT OWNER CHNAGE msg\n");
			msg_cnt.slot_owner_change_cnt++;
			break;
		case MSG_PUT_REQ:
			printf("It is a PUT REQUEST msg\n");
			msg_cnt.put_req_cnt++;
			break;
		case MSG_PUT_RESP:
			printf("It is a PUT RESPONSE msg\n");
			msg_cnt.put_resp_cnt++;
			break;
		case MSG_GET_REQ:
			printf("It is a GET REQUEST msg\n");
			msg_cnt.get_req_cnt++;
			break;
		case MSG_GET_RESP:
			printf("It is a GET RESPONSE msg\n");
			msg_cnt.get_resp_cnt++;
			break;
		case MSG_INVALIDATE_KEYS_REQ:
			printf("It is a INVALIDATE REQUEST msg\n");
			msg_cnt.invalidate_req_cnt++;
			break;
		case MSG_INVALIDATE_KEYS_RESP:
			printf("It is a INVALIDATE RESPONSE msg\n");
			msg_cnt.invalidate_resp_cnt++;
			break;
		default:
			printf("UNKNOWN msg\n");
	}
}

void process_ping_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	if(src_node->status == PFAIL)
	{
		src_node->status = ON;
		src_node->pfail_cnt = 0;
	}
	//parse and compare nodes and slots infomation with my store
	swarmkv_parse_node_and_slot(store, msg->data.swarmkv_basic_info);
	//send a PONG response
	size_t pong_msg_len = -1;
	char *pong_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	pong_msg_len = swarmkv_msg_pack_pong(store, pong_msg, SWARMKV_MAX_MSG_SIZE);
	assert(pong_msg_len > 0);
	swarmkv_msg_send(store->sockfd, pong_msg, pong_msg_len, src_node->ip, src_node->port);
	free(pong_msg);
	pong_msg = NULL;
}

void process_pong_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	src_node->pong_received_time = clock();
	src_node->ping_sent_time = 0;

	//update the status of src_node to ON
	if(src_node->status == PFAIL)
	{
		src_node->status = PFAIL;
		src_node->pfail_cnt = 0;
	}
	//parse and compare nodes and slots infomation with my store
	swarmkv_parse_node_and_slot(store, msg->data.swarmkv_basic_info);
}

void process_meet_req_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	if(src_node == NULL)
	{
		printf("Meet a new node\n");
		src_node = ALLOC(struct swarmkv_node, 1);
		src_node->ip = ALLOC(char, strlen(msg->src_ip));
		src_node->node_id = msg->src_node_id;
		HASH_ADD_INT(store->nodes, node_id, src_node);
	}
	else
	{
		printf("Meet a exist failed node\n");
	}
	printf("%s\n", msg->src_ip);
	memcpy(src_node->ip, msg->src_ip, strlen(msg->src_ip));
	src_node->port = msg->src_port;
	src_node->ping_sent_time = 0;
	src_node->pong_received_time = 0;
	src_node->status = ON;
	src_node->pfail_cnt = 0;
	//printf("src_node->ip: %s\n", src_node->ip);

	//send a meet response msg
	size_t meet_resp_msg_len = -1;
	char *meet_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	meet_resp_msg_len = swarmkv_msg_pack_meet_resp(store, meet_resp_msg, SWARMKV_MAX_MSG_SIZE);
	assert(meet_resp_msg > 0);
	swarmkv_msg_send(store->sockfd, meet_resp_msg, meet_resp_msg_len, src_node->ip, src_node->port);
	free(meet_resp_msg);
	meet_resp_msg = NULL;
}

void process_meet_resp_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	if(src_node == NULL)
	{
		src_node = ALLOC(struct swarmkv_node, 1);
		src_node->node_id = msg->src_node_id;
		HASH_ADD_INT(store->nodes, node_id, src_node);
	}
	src_node->ip = msg->src_ip;
	src_node->port = msg->src_port;
	src_node->ping_sent_time = 0;
	src_node->pong_received_time = 0;
	src_node->status = ON;
	src_node->pfail_cnt = 0;
	//swarmkv_parse_node_and_slot(store, msg->data.swarmkv_basic_info);
	swarmkv_init_node_and_slot(store, msg->data.swarmkv_basic_info);

	//caculate slot table
	struct swarmkv_slot *new_slots = ALLOC(struct swarmkv_slot, SWARMKV_DEFAULT_SLOT_NUM);
	caculate_slot_table(store, new_slots);
	size_t migrate_req_msg_len = -1;
	for(int i=0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
	{
		//重新计算后，属于自己的slot，发送迁移请求
		//send migreate key request
		if(new_slots[i].owner_node_id == store->my_node_id)
		{
			struct swarmkv_node *curr_node = NULL;
			HASH_FIND_INT(store->nodes, &store->slots[i].owner_node_id, curr_node);
			if(curr_node == NULL)
			{
				printf("I can't find the current owner node of the slot %d", i);
				continue;
			}
			char *migrate_req_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			migrate_req_msg_len = swarmkv_msg_pack_migration_req(store, migrate_req_msg, SWARMKV_MAX_MSG_SIZE, new_slots[i].slot_id);
			assert(migrate_req_msg_len>0);
			swarmkv_msg_send(store->sockfd, migrate_req_msg, migrate_req_msg_len, curr_node->ip, curr_node->port);
			free(migrate_req_msg);
			migrate_req_msg = NULL;
			/*
			//把slot的所有权改为自己
			store->slots[i].last_owner_id = store->slots[i].owner_node_id;
			store->slots[i].owner_node_id = store->my_node_id;
			*/
		}
	}
}

void process_migrate_req_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	pthread_rwlock_wrlock(&store->rwlock);
	store->slots[msg->data.migrate_req_data.slot_id].status = 1;  //migrating,该状态的slot不会再对外宣称是自己的slot
	
	struct swarmkv_table* table = NULL;
	struct swarmkv_table *tmp = NULL;
	struct swarmkv_migrate_kv mkv[SWARMKV_ONCE_MIGRATE_KV_NUM];
	int i=0;
	HASH_ITER(hh, store->table_hash, table, tmp)
	{
		//printf(">>>>>>>>>>>>>><<<<<<<<<<<<<<<<,,\n");
		struct swarmkv_key_value *owned_kv = NULL;
		struct swarmkv_key_value *owned_kv_tmp = NULL;
		char *migrate_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		HASH_ITER(hh, table->owned_value_hash, owned_kv, owned_kv_tmp)
		{
			int slot_id = caculate_key_slot_id(store, owned_kv->key, owned_kv->keylen);
			//printf("----------------  %d==%d\n",msg->data.migrate_req_data.slot_id,slot_id);
			if(msg->data.migrate_req_data.slot_id == slot_id)
			{
				mkv[i].key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
				mkv[i].value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
				base64_encode(owned_kv->key, owned_kv->keylen, mkv[i].key);
				base64_encode(owned_kv->value, owned_kv->vallen, mkv[i].value);
				printf("%d\n", i);
				mkv[i].table_name = table->table_name;
				
				i++;
				if(i >= SWARMKV_ONCE_MIGRATE_KV_NUM)
				{
					//发包
					int msg_len = swarmkv_msg_pack_migration_resp(store, migrate_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.migrate_req_data.slot_id, mkv, i);
					assert(msg_len>0);
					swarmkv_msg_send(store->sockfd, migrate_resp_msg, msg_len, src_node->ip, src_node->port);
					free(migrate_resp_msg);
					migrate_resp_msg = NULL;
					for(int j=0; j<i; j++)
					{
						free(mkv[j].key);
						mkv[j].key=NULL;
						free(mkv[j].value);
						mkv[j].value=NULL;
					}
					i = 0;
				}
			}
		}
		if(i>0)
		{
			int msg_len = swarmkv_msg_pack_migration_resp(store, migrate_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.migrate_req_data.slot_id, mkv, i);
			assert(msg_len>0);
			swarmkv_msg_send(store->sockfd, migrate_resp_msg, msg_len, src_node->ip, src_node->port);
			free(migrate_resp_msg);
			migrate_resp_msg = NULL;
			for(int j=0; j<i; j++)
			{
				free(mkv[j].key);
				mkv[j].key=NULL;
				free(mkv[j].value);
				mkv[j].value=NULL;
			}
			i = 0;
		}
	}

	//迁移完就把slot_owner_id改为请求者的id
	//change the slot owner on my store->slots
	store->slots[msg->data.migrate_req_data.slot_id].last_owner_id = store->my_node_id;
	store->slots[msg->data.migrate_req_data.slot_id].owner_node_id = src_node->node_id;
	store->slots[msg->data.migrate_req_data.slot_id].status = 0;

	//广播一条消息，告诉大家这个slot的持有者改变了
	char *slot_owner_change_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	int slot_owner_change_msg_len = swarmkv_msg_pack_slot_owner_change(store, slot_owner_change_msg, SWARMKV_MAX_MSG_SIZE, msg->data.migrate_req_data.slot_id, src_node->node_id);
	assert(slot_owner_change_msg_len>0);
	swarmkv_msg_send(store->sockfd, slot_owner_change_msg, slot_owner_change_msg_len, src_node->ip, src_node->port);
	free(slot_owner_change_msg);
	slot_owner_change_msg = NULL;

	pthread_rwlock_unlock(&store->rwlock);	
}

void process_migrate_resp_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	pthread_rwlock_wrlock(&store->rwlock);

	char *key = NULL;
	char *value = NULL;
	size_t keylen = -1;
	size_t vallen = -1;
	key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	
	for(int i=0; i<msg->data.migrate_resp_data.kv_num; i++)
	{
		memset(key, 0, SWARMKV_MAX_KEY_LEN);
		memset(value, 0, SWARMKV_MAX_VAL_LEN);
		struct swarmkv_table *table = NULL;
		struct swarmkv_key_value *kv = NULL;
		kv = ALLOC(struct swarmkv_key_value, 1);

		//decode key and value
		keylen = base64_decode(msg->data.migrate_resp_data.mkv[i].key, strlen(msg->data.migrate_resp_data.mkv[i].key), key);
		vallen = base64_decode(msg->data.migrate_resp_data.mkv[i].value, strlen(msg->data.migrate_resp_data.mkv[i].value), value);
		//printf("%s\n", key);

		HASH_FIND_STR(store->table_hash, msg->data.migrate_resp_data.mkv[i].table_name, table);
		if(table == NULL)
		{
			table = ALLOC(struct swarmkv_table, 1);
			memcpy(table->table_name, msg->data.migrate_resp_data.mkv[i].table_name, strlen(msg->data.migrate_resp_data.mkv[i].table_name));
			HASH_ADD_STR(store->table_hash, table_name, table);
		}
		HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
		if(kv == NULL)
		{
			//add a new key
			kv = ALLOC(struct swarmkv_key_value, 1);
			kv->key = ALLOC(char, keylen);
			memcpy(kv->key, key, keylen);
			HASH_ADD_KEYPTR(hh, table->owned_value_hash, kv->key, keylen, kv);
			utarray_new(kv->nodes_have_key_cached,&ut_int_icd);

			kv->keylen = keylen;
			kv->value = ALLOC(char, vallen);
			memcpy(kv->value, value, vallen);
			kv->vallen = vallen;
		}
		else
		{
			if(strcmp(kv->key, key)!=0)
			{
				collision_cnt++;
				printf("collision\n");
			}
			else
			{
				free(kv->value);
				kv->value = NULL;
				kv->value = ALLOC(char, vallen);
				memcpy(kv->value, value, vallen);
				kv->vallen = vallen;
			}
		}

		//对每一个kv,发送一个del响应，表示自己已经收到某个kv并保存，原始节点可以删除该kv
		char *del_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);

		size_t msg_len = swarmkv_msg_pack_del_msg(store, del_msg, SWARMKV_MAX_MSG_SIZE, table->table_name, msg->data.migrate_resp_data.mkv[i].key, msg->data.migrate_resp_data.mkv[i].value);
		assert(msg_len>0);
		swarmkv_msg_send(store->sockfd, del_msg, msg_len, src_node->ip, src_node->port);
		free(del_msg);
		del_msg = NULL;
	}
	free(key);
	key = NULL;
	free(value);
	value = NULL;

	pthread_rwlock_unlock(&store->rwlock);
}

void process_del_kv_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	char *key = NULL;
	char *value = NULL;
	key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	value = ALLOC(char, SWARMKV_MAX_VAL_LEN);
	size_t keylen = base64_decode(msg->data.del_req_data.key, strlen(msg->data.del_req_data.key), key);
	size_t vallen = base64_decode(msg->data.del_req_data.value, strlen(msg->data.del_req_data.value), value);
	int slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	//printf("%d %d\n",slot_owner_id, src_node->node_id);
	if(slot_owner_id == src_node->node_id)
	{	
		pthread_rwlock_wrlock(&store->rwlock);

		struct swarmkv_table *table = NULL;
		struct swarmkv_key_value *kv = NULL;
		HASH_FIND_STR(store->table_hash, msg->data.del_req_data.table_name, table);
		if(table != NULL)
		{
			HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
			if(kv != NULL)
			{
				//printf("find the kv to delete  %s:%s\n", kv->key, kv->value);
				HASH_DEL(table->owned_value_hash, kv);
				free(kv);
				kv = NULL;
			}
			/*
			//检查是不是删除掉了
			HASH_FIND(hh, table->owned_value_hash, key, keylen, kv);
			if(kv==NULL)
			{
				printf("delete success!\n");
			}
			*/
		}
		pthread_rwlock_unlock(&store->rwlock);
	}
}

void process_slot_owner_change_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	pthread_rwlock_wrlock(&store->rwlock);

	int slot_id = msg->data.slot_owner_change_data.slot_id;
	//只能接受原来的slot持有者的更新广播
	if(store->slots[slot_id].owner_node_id == src_node->node_id)
	{
		store->slots[slot_id].owner_node_id = msg->data.slot_owner_change_data.new_owner_id;
	}
	pthread_rwlock_unlock(&store->rwlock);
}

void process_get_req_msg(struct swarmkv_store* store, char *buf, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	printf("start get\n");
	char *key = NULL;
	key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	int keylen = base64_decode(msg->data.get_req_data.key, strlen(msg->data.get_req_data.key), key);

	char *get_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	int msg_len = -1;
	//check the key is belong to me?
	int slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	if(slot_owner_id != store->my_node_id)
	{
		if(msg->ttl > 0)
		{
			struct swarmkv_node *own_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, own_node);
			if(own_node == NULL)
			{
				printf("get error.\n");
				//返回一个“找不到目标key“的消息
				msg_len = swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, 
										msg->data.get_req_data.key, NULL, REQ_NODE_ERROR);
				assert(msg_len>0);
				swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, src_node->ip, src_node->port);
			}
			else
			{
				//修改ttl=ttl-1，然后重新发给我认为的slot_owner
				msg_len = swarmkv_msg_repack_ttl(buf, get_resp_msg, SWARMKV_MAX_MSG_SIZE);
				assert(msg_len>0);
				swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, own_node->ip, own_node->port);
			}
		}
		else
		{
			msg_len = swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, 
										msg->data.get_req_data.key, NULL, REQ_NODE_ERROR);
			assert(msg_len>0);
			swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, src_node->ip, src_node->port);
		}		
	}
	else
	{
		printf("it is my slot\n");
		char *value = NULL;         //记得else结束时释放这个value
		size_t vallen;
		value = swarmkv_owner_get_kv(store, msg->data.get_req_data.table_name, key, keylen, &vallen, src_node->node_id);
		if(value == NULL)          //说明这个slot确实属于我，但是我没有找到对应的value
		{
			printf("get key %s from my store for node %d error.\n", key, src_node->node_id);
		}
		
		//encode value
		char *value_base64 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
		base64_encode(value, vallen, value_base64);
		//printf("find value in my slot: %s\n", value_base64);
		if(msg->data.get_req_data.cb_function == 0)
		{
			printf("*** %s %s %s\n",msg->data.get_req_data.table_name, msg->data.get_req_data.key, value_base64);
			msg_len = swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, msg->data.get_req_data.key, value_base64, OP_SUCCESS);
		}
		else
		{
			printf("****!!\n");
			msg_len = swarmkv_msg_pack_get_resp_with_cb(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, msg->data.get_req_data.key, 
								value_base64, OP_SUCCESS, msg->src_uuid, msg->data.get_req_data.cb_function, msg->data.get_req_data.cb_arg);
		}
		assert(msg_len>0);
		swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, src_node->ip, src_node->port);
		free(value);
		value = NULL;
	}
	free(get_resp_msg);
	get_resp_msg = NULL;
	free(key);
	key = NULL;
}

void process_get_resp_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	//decode key-value
	char *key = NULL;
	char *value = NULL;
	key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	int keylen = base64_decode(msg->data.get_resp_data.key, strlen(msg->data.get_resp_data.key), key);
	int vallen = base64_decode(msg->data.get_resp_data.value, strlen(msg->data.get_resp_data.value), value);	
	
	//update my cache	
	pthread_rwlock_wrlock(&store->rwlock);
	struct swarmkv_table *table = NULL;
	struct swarmkv_key_value *kv = NULL;
	HASH_FIND_STR(store->table_hash, msg->data.get_resp_data.table_name, table);
	if(table == NULL)
	{
		//add a new table in my swarmkv_store
		table = ALLOC(struct swarmkv_table, 1);
		memcpy(table->table_name, msg->data.get_resp_data.table_name, strlen(msg->data.get_resp_data.table_name));
		HASH_ADD_STR(store->table_hash, table_name, table);
	}
	//更新缓存
	HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
	if(kv == NULL)
	{
		//add a new key
		kv = ALLOC(struct swarmkv_key_value, 1);
		kv->key = ALLOC(char, keylen);
		memcpy(kv->key, key, keylen);
		HASH_ADD_KEYPTR(hh, table->cached_value_hash, kv->key, keylen, kv);
		kv->keylen = keylen;
		kv->value = ALLOC(char, keylen);
		kv->value = value;
		kv->vallen = vallen;
	}
	else
	{
		if(strcmp(kv->key, key)!=0)
		{
			collision_cnt++;
			printf("collision\n");
		}
		else
		{
			free(kv->value);
			kv->value = NULL;
			kv->value = ALLOC(char, keylen);
			kv->value = value;
			kv->vallen = vallen;
		}
	}

	/*
	printf("if cache success\n");
	HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
	if(kv!=NULL)
	{
		printf("%s %s\n", kv->key, kv->value);
	}
	*/
	pthread_rwlock_unlock(&store->rwlock);
	//callback
	if(msg->data.get_resp_data.cb_function > 0)
	{
		if(strcmp(msg->data.get_resp_data.uuid_version, store->uuid)==0)
		{
			printf("same uuid\n");
			swarmkv_callback_func_t *cb_func = (swarmkv_callback_func_t*)msg->data.get_resp_data.cb_function;
			void *cb_arg = NULL;
			if(msg->data.get_resp_data.cb_arg > 0)
			{
				cb_arg = (void*)((unsigned long int)msg->data.get_resp_data.cb_arg);
			}
			cb_func(table->table_name, key, keylen, value, vallen, cb_arg);
		}
	}
}

void process_put_req_msg(struct swarmkv_store* store, char *buf, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	printf("start process put req\n");
	
	char *key = NULL;
	char *value = NULL;
	key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
	size_t keylen = base64_decode(msg->data.put_req_data.key, strlen(msg->data.put_req_data.key), key);
	//check slot is belong to me.
	int msg_len = 0;
	char *put_resp_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	struct swarmkv_node *aim_node = NULL;

	int slot_owner_id = caculate_key_slot_owner(store, key, keylen);
	if(slot_owner_id != store->my_node_id)
	{
		if(msg->ttl > 0)
		{
			struct swarmkv_node *own_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, own_node);
			if(own_node == NULL)
			{
				printf("put error.\n");
				//返回一个错误消息，因为slot不属于我，而且我也找不到正确的节点
				msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
														msg->data.put_req_data.key, msg->data.put_req_data.value, REQ_NODE_ERROR);
				assert(msg_len > 0);
				aim_node = src_node;
				//swarmkv_msg_send(store->sockfd, put_resp_msg, msg_len, src_node->ip, src_node->port);
			}
			else
			{
				msg_len = swarmkv_msg_repack_ttl(buf, put_resp_msg, SWARMKV_MAX_MSG_SIZE);
				assert(msg_len > 0);
				aim_node = own_node;
				//swarmkv_msg_send(store->sockfd, buf, msg_len, own_node->ip, own_node->port);
			}
		}
		else
		{
			//返回REQ_NODE_ERROR消息
			msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
														msg->data.put_req_data.key, msg->data.put_req_data.value, REQ_NODE_ERROR);
			assert(msg_len > 0);
			aim_node = src_node;
			//swarmkv_msg_send(store->sockfd, put_resp_msg, msg_len, src_node->ip, src_node->port);
		}
	}
	else
	{
		value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
		size_t vallen = base64_decode(msg->data.put_req_data.value, strlen(msg->data.put_req_data.value), value);
		aim_node = src_node;
		//put the key-value into my store and send invalidate request msg
		swarmkv_owner_put_kv(store, msg->data.put_req_data.table_name, key, keylen, value, vallen);
		swarmkv_msg_send_invalidate_key_req(store, msg->data.put_req_data.table_name, key, keylen, value, vallen);

		//send a put response
		if(msg->data.put_req_data.cb_function == 0)
		{
			msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
															msg->data.put_req_data.key, msg->data.put_req_data.value, OP_SUCCESS);
			assert(msg_len > 0);
		}
		else
		{
			printf("pack put resp with callback\n");
			msg_len = swarmkv_msg_pack_put_resp_with_cb(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
															msg->data.put_req_data.key, msg->data.put_req_data.value, OP_SUCCESS, msg->src_uuid, 
															msg->data.put_req_data.cb_function, msg->data.put_req_data.cb_arg);
			assert(msg_len > 0);
		}
	}
	swarmkv_msg_send(store->sockfd, put_resp_msg, msg_len, aim_node->ip, aim_node->port);
	free(put_resp_msg);
	put_resp_msg = NULL;
	free(key);
	key = NULL;
	free(value);
	value = NULL;
}

void process_put_resp_msg(struct swarmkv_store* store, struct swarmkv_msg *msg, struct swarmkv_node *src_node)
{
	if(msg->data.put_resp_data.op_result == OP_SUCCESS)
	{
		if(msg->data.put_resp_data.cb_function > 0)
		{
			printf("put callback\n");
			printf("%ld\n", msg->data.put_resp_data.cb_function);
			char *key = NULL;
			char *value = NULL;
			key = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			value = ALLOC(char, SWARMKV_MAX_KEY_LEN);
			int keylen = base64_decode(msg->data.put_resp_data.key, strlen(msg->data.put_resp_data.key), key);
			int vallen = base64_decode(msg->data.put_resp_data.value, strlen(msg->data.put_resp_data.value), value);
			if(strcmp(msg->data.put_resp_data.uuid_version, store->uuid) == 0)
			{
				swarmkv_callback_func_t *cb_func = (swarmkv_callback_func_t*)msg->data.put_resp_data.cb_function;
				void *cb_arg = NULL;
				if(msg->data.put_resp_data.cb_arg > 0)
				{
					cb_arg = (void*)((unsigned long int)msg->data.put_resp_data.cb_arg);
				}
				cb_func(msg->data.put_resp_data.table_name, key, keylen, value, vallen, cb_arg);
			}
			free(key);
			key = NULL;
			free(value);
			value = NULL;
		}
		else
		{
			printf("put successed!\n");
		}
	}
	else if(msg->data.put_resp_data.op_result == REQ_NODE_ERROR)
	{
		printf("PUT ERROR! Node %d is not the owner of the key.\n", src_node->node_id);
	}
}

int swarmkv_msg_unpack(struct swarmkv_store* store, char *buf)
{
	//parse cjson message to swarmkv_msg struct
	struct swarmkv_msg *msg = ALLOC(struct swarmkv_msg, 1);
	int unpack_res = swarmkv_js_msg_unpack(buf, msg);
    assert(unpack_res>0);

	msg_statistics(msg->type);    	//统计各类数据包个数
	
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
			process_ping_msg(store, msg, src_node);
			break;
		case MSG_PONG:
			process_pong_msg(store, msg, src_node);
			break;

		case MSG_MEET_REQ:
			process_meet_req_msg(store, msg, src_node);
			break;

		case MSG_MEET_RESP:			
			process_meet_resp_msg(store, msg, src_node);
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
				swarmkv_msg_send(store->sockfd, fail_resp_msg, msg_len, src_node->ip, src_node->port);
				free(fail_resp_msg);
				fail_resp_msg = NULL;
			}
			break;

		case MSG_FAIL_RESP:
			break;

		case MSG_PUT_REQ:
			process_put_req_msg(store, buf, msg, src_node);
			break;
			/*
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
					swarmkv_msg_send(store->sockfd, buf, msg_len, own_node->ip, own_node->port);
					break;
				}
				else
				{
					//返回REQ_NODE_ERROR消息
					msg_len = swarmkv_msg_pack_put_resp(store, MSG_PUT_RESP, put_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.put_req_data.table_name, 
																msg->data.put_req_data.key, msg->data.put_req_data.value, REQ_NODE_ERROR);
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
				if(msg->data.get_req_data.cb_function==0)
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
			swarmkv_msg_send(store->sockfd, put_resp_msg, msg_len, src_node->ip, src_node->port);
			free(put_resp_msg);
			put_resp_msg = NULL;
			*/	

		case MSG_PUT_RESP:
			process_put_resp_msg(store, msg, src_node);
			break;
			/*
			if(msg->data.put_resp_data.op_result == OP_SUCCESS)
			{
				if(msg->data.put_resp_data.uuid_version == store->uuid)
				{
					swarmkv_callback_func_t *cb_func = (swarmkv_callback_func_t*)msg->data.put_resp_data.cb_function;
					void *cb_arg = (void*)((unsigned long int)msg->data.put_resp_data.cb_arg);
					cb_func(table->table_name, key, keylen, value, vallen, cb_arg);
				}
			}
			else if(msg->data.put_resp_data.op_result == REQ_NODE_ERROR)
			{
				printf("The key owner you requested is wrong.\n");
			}
			*/
			
		
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
					swarmkv_msg_send(store->sockfd, invalidate_key_resp_msg, msg_len, src_node->ip, src_node->port);
					free(invalidate_key_resp_msg);
					invalidate_key_resp_msg = NULL;
				}
			}
			pthread_rwlock_unlock(&store->rwlock);
			break;

		case MSG_INVALIDATE_KEYS_RESP:
			break;
		
		case MSG_GET_REQ:	
			process_get_req_msg(store, buf, msg, src_node);
			break;
			/*
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
					swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, own_node->ip, own_node->port);
					break;
				}
				else
				{
					swarmkv_msg_pack_get_resp(store, get_resp_msg, SWARMKV_MAX_MSG_SIZE, msg->data.get_req_data.table_name, 
												msg->data.get_req_data.key, NULL, REQ_NODE_ERROR);
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
			char *value_base64 = ALLOC(char, SWARMKV_MAX_VAL_LEN);
			base64_encode(value, *value_len, value_base64);
			if(msg->data.get_req_data.cb_function==0)
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
			swarmkv_msg_send(store->sockfd, get_resp_msg, msg_len, src_node->ip, src_node->port);
			free(get_resp_msg);
			get_resp_msg = NULL;
			free(value_len);
			value_len = NULL;
			*/	

		case MSG_GET_RESP:
			process_get_resp_msg(store, msg, src_node);
			break;
			/*
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
				//HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
				HASH_ADD_KEYPTR(hh, table->cached_value_hash, kv->key, keylen, kv);
			}
			HASH_FIND(hh, table->cached_value_hash, key, keylen, kv);
			if(kv == NULL)
			{
				//add a new key
				kv = ALLOC(struct swarmkv_key_value, 1);
				kv->key = key;
				//HASH_ADD(hh, table->cached_value_hash, key, keylen, kv);
				HASH_ADD_KEYPTR(hh, table->cached_value_hash, kv->key, keylen, kv);
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
			}*/

		case MSG_MIGRATE_REQ:
			process_migrate_req_msg(store, msg, src_node);
			/*
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
			//迁移完就把slot_owner_id改为请求者的id
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
			
			swarmkv_msg_send(store->sockfd, migrate_resp_msg, msg_len, src_node->ip, src_node->port);
			
			pthread_rwlock_unlock(&store->rwlock);
			free(migrate_resp_msg);
			migrate_resp_msg = NULL;
			*/
			break;

		case MSG_MIGRATE_RESP:
			process_migrate_resp_msg(store, msg, src_node);
			/*
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
			*/
			break;
		case MSG_DEL_KV:
			process_del_kv_msg(store, msg, src_node);
			break;

		case MSG_SLOT_OWNER_CHANGE:
			process_slot_owner_change_msg(store, msg, src_node);
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

//select a node: has minimal pong_received_time
int swarmkv_random_ping(int sockfd, struct swarmkv_store *store)
{
	int nodes_cnt = HASH_COUNT(store->nodes);
	//printf("%d\n", nodes_cnt);
	uint32_t min_pong = 0;
	struct swarmkv_node *min_pong_node = NULL;

	//如果只有一个node，说明当前节点只知道自己，还没有其他已知peer nodes，不做ping操作
	if(nodes_cnt <= 1)
	{
		printf("there is no peer nodes\n");
		return 1;
	}

	//select a node to send ping msg
	int choose_num = 5;
	if(nodes_cnt-1 < choose_num)
	{
		choose_num = nodes_cnt-1;
	}
	
	int node_id_arr[nodes_cnt-1];
	struct swarmkv_node *node = NULL;
	struct swarmkv_node *tmp = NULL;
	int i = 0;
	HASH_ITER(hh, store->nodes, node, tmp)
	{
		if(node->node_id != store->my_node_id)
		{	
			node_id_arr[i] = node->node_id;
			i++;
		}
	}

	struct swarmkv_node *candidate_node = NULL;
	for (i = 0; i < choose_num; i++)
	{
		/*
		//问题：建立在node_id从0开始，连续递增的基础上，这样随机出来的数才一定会匹配到node_id
		//解决：修改，先遍历所有的node_id存储到数组中，然后从数组中随机抽取5个node_id
		int node_id = rand()%nodes_cnt;    
		if(node_id == store->my_node_id)
		{
			continue;
		}
		*/
		int node_id = node_id_arr[rand()%(nodes_cnt-1)];
		HASH_FIND_INT(store->nodes, &node_id, candidate_node);
		if(candidate_node == NULL)
		{
			continue;
		}
		if(candidate_node->ping_sent_time != 0)
		{
			//recently sent a ping, and did not receive the corresponding pong.
			//近期发送过ping_msg并且还没有收到pong_msg
			continue;
		}
		if(min_pong_node == NULL || min_pong > candidate_node->pong_received_time)
		{
			min_pong = candidate_node->pong_received_time;
			min_pong_node = candidate_node;
		}
	}
	if(min_pong_node)
	{
		printf("send a ping to random node %d\n", min_pong_node->node_id);
		char *ping_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
		size_t msg_len = swarmkv_msg_pack_ping(store, ping_msg, SWARMKV_MAX_MSG_SIZE);
		if(msg_len <= 0)
		{
			printf("pack a ping message error\n");
			return -1;
		}
		swarmkv_msg_send(store->sockfd, ping_msg, msg_len, min_pong_node->ip, min_pong_node->port);
		min_pong_node->ping_sent_time = clock();
		min_pong_node->pong_received_time = 0;

		free(ping_msg);
		ping_msg = NULL;
	}
	return 1;
}

//ping the nodes: 
//never ping before (i.e. pong_received_time=0, never receive a pong) 
//or elapsed_time - pong_received_time > TIMEOUT/2
int swarmkv_regular_ping(struct swarmkv_store *store, clock_t elapsed_time)
{
	struct swarmkv_node *node = NULL;
	struct swarmkv_node *tmp = NULL;

	//for (node = store->nodes; node != NULL; node = (struct swarmkv_node *)(node->hh.next))
	//修改hash遍历方式
	HASH_ITER(hh, store->nodes, node, tmp)
	{
		
		if (node->node_id == store->my_node_id)
		{
			continue;
		}
		if (node->status == FAIL)
		{
			continue;
		}
		//if there is a node: elapsed_time - pong_sent_time>timeout/2, send a ping msg immediately
		//if (node->pong_received_time == 0 || elapsed_time - node->pong_received_time > SWARMKV_DEFAULT_NODE_TIMEOUT_US/2)
		if (elapsed_time - node->pong_received_time > SWARMKV_DEFAULT_NODE_TIMEOUT_US/2)
		{
			printf("send a regular ping to node %d\n", node->node_id);
			char *ping_msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
			size_t msg_len = swarmkv_msg_pack_ping(store, ping_msg, SWARMKV_MAX_MSG_SIZE);
			if(msg_len <= 0)
			{
				printf("Pack ping msg error.\n");
				return -1;
			}
			swarmkv_msg_send(store->sockfd, ping_msg, msg_len, node->ip, node->port);
			node->ping_sent_time = clock();
			free(ping_msg);
			ping_msg = NULL;
			continue;
		}
		//if there is a node: elapsed_time - pong_sent_time >timeout, suspect that the node is failed
		if (elapsed_time - node->pong_received_time > SWARMKV_DEFAULT_NODE_TIMEOUT_US)
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
	char recvbuf[BUFSIZE];
	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(struct sockaddr_in);
	int ping_cnt = 0;
	int period_cnt = 0;
	while(1)
	{
		memset(recvbuf, 0, sizeof(recvbuf));
		int n = recvfrom(store->sockfd, recvbuf, BUFSIZE, 0, (struct sockaddr *)&peer_addr, &peer_addr_len);
		
		if(n > 0) 
		{
			printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
			swarmkv_msg_unpack(store, recvbuf);	
		}
		
		clock_t elapsed_time = clock();
		//Traverse all nodes per 100ms
		if(elapsed_time/SWARMKV_REGULAR_PING_INTERVAL_US >= period_cnt)
		{
			period_cnt++;
			swarmkv_regular_ping(store, elapsed_time);
		}
		
		//ping a random node per second
		if(elapsed_time/SWARMKV_RANDOM_PING_INTERVAL_US >= ping_cnt)
		{
			//printf("select a random node\n");
			ping_cnt++;
			//printf("next ping\n");
			swarmkv_random_ping(store->sockfd, store);
		}	
		
	}
	pthread_exit(NULL);
}

// config example: "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
int swarmkv_parse_config_info(struct config_information *config_info, const char *config)
{
	int config_info_len = strlen(config);
	char *config_copy = ALLOC(char, config_info_len);
	memcpy(config_copy, config, config_info_len);
	
	char *p = NULL;
	char *id_info = strtok_r(config_copy, ";", &p);
	if(id_info == NULL || p == NULL)
	{
		printf("Please set your node id. Format: node_id=id;token=xxxxx,db=0\n");
		free(config_copy);
		config_copy = NULL;
		return -1;
	}

	//id_info:node_id=id  p:token=xxxxx,db=0
	char* token_info = NULL;
	token_info = strtok_r(NULL, ",", &p);    //token_info:token=xxxxx  p:db=0
	if(token_info == NULL || p == NULL)
	{
		printf("Please set your node id. Format: node_id=id;token=xxxxx,db=0\n");
		free(config_copy);
		config_copy = NULL;
		return -1;
	}
	char *db_info = p;

	strtok_r(id_info, "=", &p);
	config_info->node_id = atoi(p);

	strtok_r(token_info, "=", &p);
	memcpy(config_info->token,p,TOKEN_LEN);

	strtok_r(db_info, "=", &p);
	config_info->db_id = atoi(p);

	free(config_copy);

	return 0;
}

//prase config and bootstraps infomation set by user
int swarmkv_config_and_init(struct swarmkv_store *store, const char *bootstraps, const char *config)
{
	//struct bootstrap_node *bs_nodes;

	struct config_information *config_info = ALLOC(struct config_information, 1);
	swarmkv_parse_config_info(config_info, config);
	store->my_node_id = config_info->node_id;
	printf("%d\n", store->my_node_id);
	/*
	int config_info_len = strlen(config);
	char *config_info = ALLOC(char, config_info_len);
	memcpy(config_info, config, config_info_len);
	
	//char *my_id_info = NULL;
	
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
	*/

	// bootstraps example: "self=0.0.0.0:8323;peers=192.168.0.100:8323,192.168.0.101:8323"
	int bs_info_len = strlen(bootstraps);
	char *bootstrap_info = ALLOC(char, bs_info_len);
	memcpy(bootstrap_info, bootstraps, bs_info_len);

	char *my_addr = NULL;
	char *my_ip = NULL;
	char *peer_node_addr = NULL;
    char *p = NULL;
	char *p1 = NULL;
	char *peer_ip = NULL;
	uint16_t peer_port;
	int meet_node_cnt = 0;

	strtok_r(bootstrap_info, ";", &p);
	if(bootstrap_info == NULL || p == NULL)
	{
		free(bootstrap_info);
		bootstrap_info = NULL;
		printf("Please set your ip&port and bootstrap_nodes ip&port. Format: self=ip:port;peers=ip0:port0,ip1:port1,...\n");
		return -1;
	}

	strtok_r(bootstrap_info, "=", &my_addr);
	//printf("my_addr:%s\n", my_addr);   //0.0.0.0:8323

	strtok_r(p, "=", &peer_node_addr);
	//printf("peer_node_addr: %s\n",peer_node_addr);   //192.168.0.100:8323,192.168.0.101:8323
	
	my_ip = strtok_r(my_addr, ":", &p);
	int ip_len = strlen(my_ip);
	store->my_ip = ALLOC(char, ip_len);
	memcpy(store->my_ip, my_ip, ip_len);
	store->my_port = atoi(p);
	printf("store->my_port:%d\n", store->my_port);
	//printf("store->my_ip: %s\n", store->my_ip);
	
	//create socket
	store->sockfd = swarmkv_create_socket(store->my_port);
	printf("store->sockfd : %d\n", store->sockfd);

	//parse bootstrap nodes and send meet requests 解析初始的meet node并发送meet request
	// peer_node_addr example : 172.16.224.160:6323,172.16.224.160:7323,172.16.224.160:18323
	peer_node_addr = strtok_r(peer_node_addr, ",", &p);
	while(peer_node_addr != NULL)
	{
		printf("peer_node_addr: %s\n",peer_node_addr);
		peer_ip = strtok_r(peer_node_addr, ":", &p1);
		peer_port = atoi(p1);
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
		swarmkv_msg_send(store->sockfd, meet_req, msg_len, peer_ip, peer_port);
		free(meet_req);
		meet_req = NULL;
		meet_node_cnt ++;
		peer_node_addr = strtok_r(NULL, ",", &p);
	}
	
	free(bootstrap_info);
	bootstrap_info = NULL;
	//printf("my_ip: %s\n", store->my_ip);
	if(meet_node_cnt == 0)
	{
		printf("error: None of the meet messages were sent successfully.\n");
		return -1;
	}
	return 1;
}

//int swarmkv_open(struct swarmkv_store *store, const char *bootstraps, const char *config, char **err)
struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err)
{
	struct swarmkv_store *store = NULL;
	store = ALLOC(struct swarmkv_store, 1);
	uuid_t uuid;
    uuid_generate(uuid); 
	uuid_unparse(uuid, store->uuid);
	
	//init, set my info(ip.port,id), parse bootstrap node information and send meet msg to them
	if(swarmkv_config_and_init(store, bootstraps, config) < 0)
	{
		printf("Startup failed, please reconfigure. Format: node_id=id;token=xxxxx,db=0\n");
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
	myself->ping_sent_time = 0;
	myself->pong_received_time = 0;
	myself->status = ON;
	myself->pfail_cnt = 0;
	HASH_ADD_INT(store->nodes, node_id, myself); 

	//init slots[SWARMKV_DEFAULT_SLOT_NUM], all slot is belong to me
	int i = 0;
	for(i = 0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
	{
		store->slots[i].slot_id = i;
		store->slots[i].owner_node_id = store->my_node_id;
		/*
		if(store->my_node_id == 0)
		{
			store->slots[i].owner_node_id = store->my_node_id;
		}
		else
		{
			//store->slots[i].owner_node_id = store->my_node_id;
			store->slots[i].owner_node_id = -1;
		}
		*/
	}
	
	//create a thread to recieve and handle remote message
	pthread_t tid;
	int pthread_ret = pthread_create(&tid, NULL, swarmkv_msg_processing_thread, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return NULL;
	}
	return store;

	/*
	// wait for processing thread close
	if (pthread_join(tid, NULL))                 //调用pthread_join()就会阻塞在这里了    
    {
        printf("thread is not exit...\n");
	}
	return 1;
	*/
	
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

void print_msg_stat()
{	
	printf("--------------------------------------------------------\n");
	printf("PING                 %d\n", msg_cnt.ping_cnt);
	printf("PONG                 %d\n", msg_cnt.pong_cnt);
	printf("MEET REQUEST         %d\n", msg_cnt.meet_req_cnt);
	printf("MEET RESPONSE        %d\n", msg_cnt.meet_resp_cnt);
	printf("FAIL REQUEST         %d\n", msg_cnt.fail_req_cnt);
	printf("FAIL RESPONSE        %d\n", msg_cnt.fail_resp_cnt);
	printf("MIGRATE REQUEST      %d\n", msg_cnt.migrate_req_cnt);
	printf("MIGRATE RESPONSE     %d\n", msg_cnt.migrate_resp_cnt);
	printf("DEL KV               %d\n", msg_cnt.del_kv_cnt);
	printf("SLOT OWNER CHANGE    %d\n", msg_cnt.slot_owner_change_cnt);
	printf("PUT REQUEST          %d\n", msg_cnt.put_req_cnt);
	printf("PUT RESPONSE         %d\n", msg_cnt.put_resp_cnt);
	printf("GET REQUEST          %d\n", msg_cnt.get_req_cnt);
	printf("GET RESPONSE         %d\n", msg_cnt.get_resp_cnt);
	printf("INVALIDATE REQUEST   %d\n", msg_cnt.invalidate_req_cnt);
	printf("INVALIDATE RESPONSE  %d\n", msg_cnt.invalidate_resp_cnt);
	printf("--------------------------------------------------------\n");
	printf("TOTAL:               %d\n", msg_cnt.total_cnt);
}

void swarmkv_close(struct swarmkv_store *store)
{
	print_msg_stat();
	close(store->sockfd);
	free(store);
	store = NULL;
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

