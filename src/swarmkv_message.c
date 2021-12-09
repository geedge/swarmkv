#include <assert.h>
//#include "./inc_internal/swarmkv_message.h"
#include "swarmkv_message.h"

//check buff_size
int buff_check(size_t buff_size)
{
	if(buff_size == 0)
	{
		return 0;
	}
	return 1;
}

int min(int a, int b)
{
	if(a < b)
		return a;
	return b;
}

//pack basic inforswarmkv_msg_pack_slot_owner_changetion: my id, ip, port, and msg_type
static void *swarmkv_msg_pack_basic_info(struct swarmkv_store *store, cJSON *js_msg, enum swarmkv_msg_type type)
{
	cJSON_AddNumberToObject(js_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_msg, "my_port", store->my_port);
	cJSON_AddStringToObject(js_msg, "uuid", store->uuid);
	cJSON_AddNumberToObject(js_msg, "TTL", SWARMKV_DEFAULT_TTL);
	cJSON_AddNumberToObject(js_msg, "msg_type", type);
}

//pack store->nodes into msg
static void *swarmkv_msg_pack_node_list(struct swarmkv_store *store, cJSON *js_msg)
{
	cJSON* js_node_list = NULL;
	cJSON* js_node = NULL;
	//js_node_list = cJSON_CreateArray();
	cJSON_AddItemToObject(js_msg,"node_list", js_node_list = cJSON_CreateArray());
	struct swarmkv_node* node = NULL;
	struct swarmkv_node* node_tmp = NULL;
	HASH_ITER(hh, store->nodes, node, node_tmp)
	{
		if(node->status != FAIL && node->status != WAITMEET)
		{
			cJSON_AddItemToArray(js_node_list, js_node = cJSON_CreateObject());
			cJSON_AddNumberToObject(js_node,"node_id", node->node_id);
			cJSON_AddStringToObject(js_node, "node_ip", node->ip);
			cJSON_AddNumberToObject(js_node,"node_port", node->port);
			cJSON_AddNumberToObject(js_node,"node_status", node->status);
		}
	}
}

//pack store->slots[SWARMKV_DEFAULT_SLOT_NUM] into msg
static void *swarmkv_msg_pack_my_slot(struct swarmkv_store *store, cJSON *js_msg)
{
	cJSON* js_slot_list = NULL;
	cJSON* js_slot = NULL;
	int i = 0;

	//js_slot_list = cJSON_CreateArray();
	cJSON_AddItemToObject(js_msg,"slot_list", js_slot_list = cJSON_CreateArray());
	for(i=0;i<SWARMKV_DEFAULT_SLOT_NUM;i++)
	{
		if(store->slots[i].owner_node_id == store->my_node_id)
		{
			if(store->slots[i].status == SLOT_MIGRATING)
			{
				continue;         //正在迁移中的slot不再对外宣告
			}
			cJSON_AddItemToArray(js_slot_list, js_slot = cJSON_CreateObject());
			cJSON_AddNumberToObject(js_slot,"sid", store->slots[i].slot_id);
			cJSON_AddNumberToObject(js_slot,"oid", store->slots[i].owner_node_id);
		}
	}
}

static void *swarmkv_msg_pack_all_slot(struct swarmkv_store *store, cJSON *js_msg)
{
	cJSON* js_slot_list = NULL;
	int i = 0;
	//js_slot_list = cJSON_CreateArray();
	cJSON_AddItemToObject(js_msg,"slot_list", js_slot_list = cJSON_CreateArray());
	for(i=0; i<SWARMKV_DEFAULT_SLOT_NUM; i++)
	{	
		//meet response还是需要所有的slot信息
		//不会有正在迁移的slot,因为slot迁移发生在meet期间，不会同时meet两个节点
		cJSON* js_slot = NULL;
		cJSON_AddItemToArray(js_slot_list, js_slot = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_slot,"sid", store->slots[i].slot_id);
		cJSON_AddNumberToObject(js_slot,"oid", store->slots[i].owner_node_id);
	}
}

static void *pack_cluster_status_json(struct swarmkv_store *store, cJSON *js_msg)
{	
	swarmkv_msg_pack_node_list(store, js_msg);
	swarmkv_msg_pack_my_slot(store, js_msg);
}

static size_t serialization_msg(cJSON *js_msg, char *buff, size_t buff_size)
{
	char* msg = NULL;
	size_t msg_len = 0;
	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);
	msg_len = min(buff_size, strlen(msg)+1);
	memcpy(buff, msg, msg_len);
	free(msg);
	msg = NULL;
	return msg_len;
}

//pack meet_req msg
size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size)
{	
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	enum swarmkv_msg_type type = MSG_MEET_REQ;
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_meet_resp(struct swarmkv_store *store, char *buff, size_t buff_size, int op_result)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	enum swarmkv_msg_type type = MSG_MEET_RESP;
	swarmkv_msg_pack_basic_info(store, js_msg, type);

	cJSON_AddNumberToObject(js_msg, "op_result", op_result);
	if(op_result == MEET_RECIEVE)
	{
		swarmkv_msg_pack_node_list(store, js_msg);
		swarmkv_msg_pack_all_slot(store, js_msg);
	}
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//pack meet_req msg
size_t swarmkv_msg_pack_join_complete(struct swarmkv_store *store, char *buff, size_t buff_size)
{	
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	enum swarmkv_msg_type type = MSG_JOIN_COMPLETE;
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_ping(struct swarmkv_store *store, char *buff, size_t buff_size)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_PING;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	pack_cluster_status_json(store, js_msg);

	msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_pong(struct swarmkv_store *store, char *buff, size_t buff_size)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_PONG;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	pack_cluster_status_json(store, js_msg);

	msg_len = serialization_msg(js_msg, buff, buff_size);

	cJSON_Delete(js_msg);
	return msg_len;
}

//pack fail req msg
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_FAIL_REQ;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "fail_node_id", fail_node_id);

	msg_len = serialization_msg(js_msg, buff, buff_size);
	
	cJSON_Delete(js_msg);  
	return msg_len;
}

size_t swarmkv_msg_pack_fail_response(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id, int op_result)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_FAIL_RESP;
	
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "fail_node_id", fail_node_id);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);

	msg_len = serialization_msg(js_msg, buff, buff_size);

	cJSON_Delete(js_msg);
	return msg_len;	
}

static void *swarmkv_msg_pack_kv_object(struct swarmkv_store *store, cJSON *js_msg, enum swarmkv_msg_type type, 
											const char* tb_name, const char* key, const char* value)
{
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddStringToObject(js_msg, "table_name", tb_name);
	cJSON_AddStringToObject(js_msg, "key", key);
	cJSON_AddStringToObject(js_msg, "value", value);
}

//pack put req msg
size_t swarmkv_msg_pack_put_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	swarmkv_msg_pack_kv_object(store, js_msg, type, tb_name, key, value);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_put_req_with_cb(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, long unsigned int callback_func_addr, long unsigned int callback_arg_addr)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	swarmkv_msg_pack_kv_object(store, js_msg, type, tb_name, key, value);
	cJSON_AddNumberToObject(js_msg, "callback_func", callback_func_addr);
	cJSON_AddNumberToObject(js_msg, "callback_func_arg", callback_arg_addr);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//pack put response msg
size_t swarmkv_msg_pack_put_resp(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, int op_result)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	swarmkv_msg_pack_kv_object(store, js_msg, type, tb_name, key, value);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_put_resp_with_cb(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, int op_result, const char* uuid_version, long unsigned int callback_func_addr, long unsigned int callback_arg_addr)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	swarmkv_msg_pack_kv_object(store, js_msg, type, tb_name, key, value);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);
	cJSON_AddNumberToObject(js_msg, "callback_func", callback_func_addr);
	cJSON_AddNumberToObject(js_msg, "callback_func_arg", callback_arg_addr);
	cJSON_AddStringToObject(js_msg, "uuid_version", uuid_version);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//pack get req msg
static void *swarmkv_msg_pack_get_req_object(struct swarmkv_store *store, cJSON *js_msg, const char *table_name, const char* key)
{
	enum swarmkv_msg_type type = MSG_GET_REQ;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddStringToObject(js_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_msg, "key", key);
}

size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();

	swarmkv_msg_pack_get_req_object(store, js_msg, table_name, key);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}
size_t swarmkv_msg_pack_get_req_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, long unsigned int callback_func_addr, long unsigned int callback_arg_addr)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();

	swarmkv_msg_pack_get_req_object(store, js_msg, table_name, key);
	cJSON_AddNumberToObject(js_msg, "callback_func", callback_func_addr);
	cJSON_AddNumberToObject(js_msg, "callback_func_arg", callback_arg_addr);

	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//pack get resp msg
static void *swarmkv_msg_pack_get_resp_object(struct swarmkv_store *store, cJSON *js_msg, const char *table_name, const char* key, const char* value, int op_result)
{
	enum swarmkv_msg_type type = MSG_GET_RESP;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddStringToObject(js_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_msg, "key", key);
	cJSON_AddStringToObject(js_msg, "value", value);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);
}

size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, const char* value, int op_result)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();

	swarmkv_msg_pack_get_resp_object(store, js_msg, table_name, key, value, op_result);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_get_resp_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, 
									const char* value, int op_result, const char* uuid_version, long unsigned int callback_func_addr, long unsigned int callback_arg_addr)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();

	swarmkv_msg_pack_get_resp_object(store, js_msg, table_name, key, value, op_result);
	cJSON_AddStringToObject(js_msg, "uuid_version", uuid_version);
	cJSON_AddNumberToObject(js_msg, "callback_func", callback_func_addr);
	cJSON_AddNumberToObject(js_msg, "callback_func_arg", callback_arg_addr);

	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_migration_req(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	enum swarmkv_msg_type type = MSG_MIGRATE_REQ;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "sid", slot_id);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);

	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_migration_resp(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id,
										struct swarmkv_migrate_kv mkv[], int mkv_num)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	cJSON* js_mkv_list = NULL;
	cJSON* js_mkv = NULL;
	enum swarmkv_msg_type type = MSG_MIGRATE_RESP;

	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "sid", slot_id);
	cJSON_AddNumberToObject(js_msg, "kv_num", mkv_num);
	cJSON_AddItemToObject(js_msg, "kv", js_mkv_list = cJSON_CreateArray());
	
	for(int i=0; i<mkv_num; i++)
	{
		cJSON_AddItemToArray(js_mkv_list, js_mkv = cJSON_CreateObject());
		cJSON_AddStringToObject(js_mkv,"table_name", mkv[i].table_name);
		cJSON_AddStringToObject(js_mkv, "key", mkv[i].key);
		cJSON_AddStringToObject(js_mkv,"value", mkv[i].value);
	}
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);

	cJSON_Delete(js_msg);
	return msg_len;
}


//pack del msg
size_t swarmkv_msg_pack_del_msg(struct swarmkv_store *store, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	enum swarmkv_msg_type type = MSG_DEL_KV;
	swarmkv_msg_pack_kv_object(store, js_msg, type, tb_name, key, value);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//slot owner change
size_t swarmkv_msg_pack_slot_owner_change(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id, int new_owner_id)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();

	enum swarmkv_msg_type type = MSG_SLOT_OWNER_CHANGE;
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "sid", slot_id);
	cJSON_AddNumberToObject(js_msg, "new_owner", new_owner_id);

	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_pack_migrate_moved(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id, int new_owner_id)
{
	cJSON* js_msg = NULL;
	js_msg = cJSON_CreateObject();
	
	enum swarmkv_msg_type type = MSG_MIGRATE_MOVED;
	swarmkv_msg_pack_basic_info(store, js_msg, type);
	cJSON_AddNumberToObject(js_msg, "sid", slot_id);
	cJSON_AddNumberToObject(js_msg, "new_owner", new_owner_id);
	
	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

size_t swarmkv_msg_repack_ttl(char *raw_msg, char *buff, size_t buff_size)
{
	cJSON* js_msg = cJSON_Parse(raw_msg);

	if(!js_msg) 
	{
    	printf("get msg faild !\n");
		return 0;
	}
	int ttl = cJSON_GetObjectItem(js_msg, "ttl")->valueint;
	cJSON_ReplaceItemInObject(js_msg, "ttl", cJSON_CreateNumber(ttl-1));

	size_t msg_len = serialization_msg(js_msg, buff, buff_size);
	cJSON_Delete(js_msg);
	return msg_len;
}

//unpack received message
int swarmkv_js_msg_unpack(char *buf, struct swarmkv_msg *msg)
{
	cJSON* js_msg = cJSON_Parse(buf);
	if(!js_msg) 
	{
    	printf("get msg faild !\n");
		return -1;
	}
	
	cJSON* src_id = cJSON_GetObjectItem(js_msg, "my_id");
	cJSON* src_ip = cJSON_GetObjectItem(js_msg, "my_ip");
	cJSON* src_port = cJSON_GetObjectItem(js_msg, "my_port");
	cJSON* src_uuid = cJSON_GetObjectItem(js_msg, "uuid");
	cJSON* ttl = cJSON_GetObjectItem(js_msg, "TTL");
	cJSON* msg_type = cJSON_GetObjectItem(js_msg, "msg_type");
	
	msg->src_node_id = src_id->valueint;
	msg->src_ip = ALLOC(char, strlen(src_ip->valuestring)+1);
	memcpy(msg->src_ip, src_ip->valuestring, strlen(src_ip->valuestring));
	msg->src_port = src_port->valueint;
	memcpy(msg->src_uuid, src_uuid->valuestring, 36);
	msg->ttl = ttl->valueint;
	msg->type = msg_type->valueint;
	
	cJSON *tb_name = NULL;
	cJSON *key = NULL;
	cJSON *value = NULL;
	cJSON *cb_function = NULL;
	cJSON *cb_arg = NULL;
	cJSON *node_list = NULL;
	cJSON *slot_list = NULL;
	cJSON *failed_node_id = NULL;
	cJSON *op_result = NULL;
	cJSON *uuid_version = NULL;
	cJSON *slot_id = NULL;
	cJSON *new_owner = NULL;
	cJSON *kv_num = NULL;
	cJSON *kv_list = NULL;
	//cJSON *node_version = NULL;
	//cJSON *slot_version = NULL;
	int node_num = 0;
	int slot_num = 0;
	int tb_name_len = 0;
	int key_len = 0;
	int val_len = 0;
	int i = 0;
	//printf("next\n");
	switch(msg->type)
	{
		case MSG_PING:
		case MSG_PONG:
			//node_version = cJSON_GetObjectItem(js_msg, "node_version");
			//msg->data.swarmkv_basic_info.node_version = node_version->valueint;
			//slot_version = cJSON_GetObjectItem(js_msg, "slot_version");
			//msg->data.swarmkv_basic_info.slot_version = slot_version->valueint;
			node_list = cJSON_GetObjectItem(js_msg, "node_list");
			slot_list = cJSON_GetObjectItem(js_msg, "slot_list");
			node_num = cJSON_GetArraySize(node_list);
			msg->data.swarmkv_basic_info.node_num = node_num;
			slot_num = cJSON_GetArraySize(slot_list);
			msg->data.swarmkv_basic_info.slot_num = slot_num;
			i = 0;
			if(node_list != NULL){
				cJSON *node_item  = node_list->child;
				while(node_item)
				{ 
					cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
					cJSON *node_status = cJSON_GetObjectItem(node_item, "node_status");
					cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
					cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");	
					msg->data.swarmkv_basic_info.nodes[i].node_id = node_id->valueint;
					int ip_len = strlen(node_ip->valuestring);
					msg->data.swarmkv_basic_info.nodes[i].ip = ALLOC(char, ip_len+1);
					memcpy(msg->data.swarmkv_basic_info.nodes[i].ip, node_ip->valuestring, ip_len);
					//msg->data.swarmkv_basic_info.nodes[i].ip = node_ip->valuestring;
					msg->data.swarmkv_basic_info.nodes[i].port = node_port->valueint;
					msg->data.swarmkv_basic_info.nodes[i].status = node_status->valueint;
					i++;
					node_item = node_item->next;
				}
			}
			i = 0;
			if(slot_list != NULL)
			{
				cJSON *slot_item = slot_list->child;
				while(slot_item)
				{
					slot_id = cJSON_GetObjectItem(slot_item, "sid");
					cJSON *owner_id = cJSON_GetObjectItem(slot_item, "oid");
					msg->data.swarmkv_basic_info.slots[i].slot_id = slot_id->valueint;
					msg->data.swarmkv_basic_info.slots[i].owner_node_id = owner_id->valueint;
					i++;
					slot_item = slot_item->next;
				}
			}
			break;
		
		case MSG_MEET_REQ:
			break;
		case MSG_MEET_RESP:
			op_result = cJSON_GetObjectItem(js_msg, "op_result");
			msg->data.meet_resp_data.op_result = op_result->valueint;
			if(op_result->valueint == MEET_RECIEVE)
			{
				node_list = cJSON_GetObjectItem(js_msg, "node_list");
				slot_list = cJSON_GetObjectItem(js_msg, "slot_list");
				node_num = cJSON_GetArraySize(node_list);
				msg->data.meet_resp_data.node_num = node_num;
				slot_num = cJSON_GetArraySize(slot_list);
				msg->data.meet_resp_data.slot_num = slot_num;
				i = 0;
				if(node_list != NULL)
				{
					cJSON *node_item  = node_list->child;
					while(node_item)
					{ 
						cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
						cJSON *node_status = cJSON_GetObjectItem(node_item, "node_status");
						cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
						cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");	
						msg->data.meet_resp_data.nodes[i].node_id = node_id->valueint;
						int ip_len = strlen(node_ip->valuestring);
						msg->data.meet_resp_data.nodes[i].ip = ALLOC(char, ip_len+1);
						memcpy(msg->data.meet_resp_data.nodes[i].ip, node_ip->valuestring, ip_len);
						msg->data.meet_resp_data.nodes[i].port = node_port->valueint;
						msg->data.meet_resp_data.nodes[i].status = node_status->valueint;
						i++;
						node_item = node_item->next;
					}
				}
				if(slot_list != NULL)
				{
					i = 0;
					cJSON *slot_item = slot_list->child;
					while(slot_item)
					{
						slot_id = cJSON_GetObjectItem(slot_item, "sid");
						cJSON *owner_id = cJSON_GetObjectItem(slot_item, "oid");
						msg->data.meet_resp_data.slots[i].slot_id = slot_id->valueint;
						msg->data.meet_resp_data.slots[i].owner_node_id = owner_id->valueint;
						i++;
						slot_item = slot_item->next;
					}
				}
			}
			break;
		case MSG_JOIN_COMPLETE:
			break;
		case MSG_FAIL_REQ:
			failed_node_id = cJSON_GetObjectItem(js_msg, "node_id");
			msg->data.fail_req_data.failed_node_id = failed_node_id->valueint;
			break;
		case MSG_FAIL_RESP:
			failed_node_id = cJSON_GetObjectItem(js_msg, "node_id");
			op_result = cJSON_GetObjectItem(js_msg, "op_result");
			msg->data.fail_resp_data.failed_node_id = failed_node_id->valueint;
			msg->data.fail_resp_data.op_result = op_result->valueint;
			break;

		case MSG_DEL_KV:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.del_req_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.del_req_data.key = ALLOC(char, key_len+1);
			msg->data.del_req_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.del_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.del_req_data.key, key->valuestring, key_len);
			memcpy(msg->data.del_req_data.value, value->valuestring, val_len);
			break;
		
		case MSG_SLOT_OWNER_CHANGE:
			slot_id = cJSON_GetObjectItem(js_msg, "sid");
			msg->data.slot_moved_data.slot_id = slot_id->valueint;
			new_owner = cJSON_GetObjectItem(js_msg, "new_owner");
			msg->data.slot_moved_data.new_owner_id = new_owner->valueint;
			break;

		case MSG_PUT_REQ:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.put_req_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.put_req_data.key = ALLOC(char, key_len+1);
			msg->data.put_req_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.put_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.put_req_data.key, key->valuestring, key_len);
			memcpy(msg->data.put_req_data.value, value->valuestring, val_len);
			cb_function = cJSON_GetObjectItem(js_msg, "callback_func");
			cb_arg = cJSON_GetObjectItem(js_msg, "callback_func_arg");
			if(cb_function == NULL)
			{
				//msg->data.put_req_data.cb_function = -1;    //cb_function设置了无符号长整形呀！！！！！不能是负数-1！笨不笨！
				msg->data.put_req_data.cb_function = 0; 
				msg->data.put_req_data.cb_arg = 0;
			}
			else
			{
				msg->data.put_req_data.cb_function = cb_function->valueint;
				msg->data.put_req_data.cb_arg = cb_arg->valueint;
				//printf("unpack cb_function->valueint: %ld\n", msg->data.put_req_data.cb_function);
				//printf("unpack cb_arg->valueint: %ld\n", msg->data.put_req_data.cb_arg);
			}
			break;
		case MSG_PUT_RESP:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.put_resp_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.put_resp_data.key = ALLOC(char, key_len+1);
			msg->data.put_resp_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.put_resp_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.put_resp_data.key, key->valuestring, key_len);
			memcpy(msg->data.put_resp_data.value, value->valuestring, val_len);
			op_result = cJSON_GetObjectItem(js_msg, "op_result");
			msg->data.put_resp_data.op_result = op_result->valueint;
			cb_function = cJSON_GetObjectItem(js_msg, "callback_func");
			cb_arg = cJSON_GetObjectItem(js_msg, "callback_func_arg");
			uuid_version = cJSON_GetObjectItem(js_msg, "uuid_version");
			if(cb_function == NULL)
			{
				msg->data.put_resp_data.cb_function = 0;
				msg->data.put_resp_data.cb_arg = 0;
			}
			else
			{
				msg->data.put_resp_data.cb_function = cb_function->valueint;
				msg->data.put_resp_data.cb_arg = cb_arg->valueint;
				memcpy(msg->data.put_resp_data.uuid_version, uuid_version->valuestring, 36);
			}
			break;
		
		case MSG_GET_REQ:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			msg->data.get_req_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.get_req_data.key = ALLOC(char, key_len+1);
			memcpy(msg->data.get_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.get_req_data.key, key->valuestring, key_len);
			cb_function = cJSON_GetObjectItem(js_msg, "callback_func");
			cb_arg = cJSON_GetObjectItem(js_msg, "callback_func_arg");
			if(cb_function == NULL)
			{
				msg->data.get_req_data.cb_function = 0;
				msg->data.get_req_data.cb_arg = 0;
			}
			else
			{
				msg->data.get_req_data.cb_function = cb_function->valueint;
				msg->data.get_req_data.cb_arg = cb_arg->valueint;
			}
			break;
		case MSG_GET_RESP:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.get_resp_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.get_resp_data.key = ALLOC(char, key_len+1);
			msg->data.get_resp_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.get_resp_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.get_resp_data.key, key->valuestring, key_len);
			memcpy(msg->data.get_resp_data.value, value->valuestring, val_len);
			cb_function = cJSON_GetObjectItem(js_msg, "callback_func");
			cb_arg = cJSON_GetObjectItem(js_msg, "callback_func_arg");
			uuid_version = cJSON_GetObjectItem(js_msg, "uuid_version");
			if(cb_function == NULL)
			{
				msg->data.get_resp_data.cb_function = 0;
				msg->data.get_resp_data.cb_arg = 0;
			}
			else
			{
				msg->data.get_resp_data.cb_function = cb_function->valueint;
				msg->data.get_resp_data.cb_arg = cb_arg->valueint;
				//printf("uuid_version->valuestring: %zu\n", strlen(uuid_version->valuestring));
				memset(msg->data.get_resp_data.uuid_version, 0, sizeof(msg->data.get_resp_data.uuid_version));
				memcpy(msg->data.get_resp_data.uuid_version, uuid_version->valuestring, strlen(uuid_version->valuestring));
				//printf("msg->data.get_resp_data.uuid_version: %s\n", msg->data.get_resp_data.uuid_version);
			}
			break;	
		
		case MSG_INVALIDATE_KEYS_REQ:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.invalidate_req_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.invalidate_req_data.key = ALLOC(char, key_len+1);
			msg->data.invalidate_req_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.invalidate_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.invalidate_req_data.key, key->valuestring, key_len);
			memcpy(msg->data.invalidate_req_data.value, value->valuestring, val_len);
			break;
		case MSG_INVALIDATE_KEYS_RESP:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.invalidate_resp_data.table_name = ALLOC(char, tb_name_len+1);
			msg->data.invalidate_resp_data.key = ALLOC(char, key_len+1);
			msg->data.invalidate_resp_data.value = ALLOC(char, val_len+1);
			memcpy(msg->data.invalidate_resp_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.invalidate_resp_data.key, key->valuestring, key_len);
			memcpy(msg->data.invalidate_resp_data.value, value->valuestring, val_len);
			op_result = cJSON_GetObjectItem(js_msg, "op_result");
			msg->data.invalidate_resp_data.op_result = op_result->valueint;
			break;

		case MSG_MIGRATE_REQ:
			slot_id = cJSON_GetObjectItem(js_msg, "sid");
			msg->data.migrate_req_data.slot_id = slot_id->valueint;
			break;
		case MSG_MIGRATE_RESP:	
			slot_id = cJSON_GetObjectItem(js_msg, "sid");
			msg->data.migrate_resp_data.slot_id = slot_id->valueint;
			kv_num = cJSON_GetObjectItem(js_msg, "kv_num");
			msg->data.migrate_resp_data.kv_num = kv_num->valueint;
			//curr_kv_num = cJSON_GetArraySize(node_list);
			msg->data.migrate_resp_data.mkv = ALLOC(struct swarmkv_migrate_kv, msg->data.migrate_resp_data.kv_num);
			kv_list = cJSON_GetObjectItem(js_msg, "kv");
			if(kv_list != NULL){
				cJSON *kv_item  = kv_list->child;
				i = 0;
				while(kv_item)
				{ 
					tb_name = cJSON_GetObjectItem(kv_item, "table_name");
					key = cJSON_GetObjectItem(kv_item, "key");
					value = cJSON_GetObjectItem(kv_item, "value");
					tb_name_len = strlen(tb_name->valuestring);
					key_len = strlen(key->valuestring);
					val_len = strlen(value->valuestring);
					msg->data.migrate_resp_data.mkv[i].table_name = ALLOC(char, tb_name_len+1);
					msg->data.migrate_resp_data.mkv[i].key = ALLOC(char, key_len+1);
					msg->data.migrate_resp_data.mkv[i].value = ALLOC(char, val_len+1);
					memcpy(msg->data.migrate_resp_data.mkv[i].table_name, tb_name->valuestring, tb_name_len);
					memcpy(msg->data.migrate_resp_data.mkv[i].key, key->valuestring, key_len);
					memcpy(msg->data.migrate_resp_data.mkv[i].value, value->valuestring, val_len);
					i++;
					kv_item = kv_item->next;
				}
			}
			break;
		case MSG_MIGRATE_MOVED:
			slot_id = cJSON_GetObjectItem(js_msg, "sid");
			msg->data.slot_moved_data.slot_id = slot_id->valueint;
			new_owner = cJSON_GetObjectItem(js_msg, "new_owner");
			msg->data.slot_moved_data.new_owner_id = new_owner->valueint;
			break;

		default:
			printf("UNKNOWN msg\n");
	}
	
	cJSON_Delete(js_msg);

	return 1;
}
