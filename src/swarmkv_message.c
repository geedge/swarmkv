#include <assert.h>
//#include "./inc_internal/swarmkv_message.h"
#include "swarmkv_message.h"

//pack basic information: my id, ip, port, and msg_type
cJSON *swarmkv_msg_pack_basic_info(struct swarmkv_store *store, enum swarmkv_msg_type type)
{
	cJSON* js_basic_info = 0;
	js_basic_info = cJSON_CreateObject();
	cJSON_AddNumberToObject(js_basic_info, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_basic_info, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_basic_info, "my_port", store->my_port);
	cJSON_AddStringToObject(js_basic_info, "uuid", store->uuid);
	cJSON_AddNumberToObject(js_basic_info, "msg_type", type);

	return js_basic_info;
}

//pack store->nodes into msg
cJSON *swarmkv_msg_pack_node_list(struct swarmkv_store *store)
{
	cJSON* js_node_list = 0;
	cJSON* js_node = 0;
	
	js_node_list = cJSON_CreateArray();
	struct swarmkv_node* node;
	for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) {
		cJSON_AddItemToArray(js_node_list, js_node = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_node,"node_id", node->node_id);
		cJSON_AddStringToObject(js_node, "node_ip", node->ip);
		cJSON_AddNumberToObject(js_node,"node_port", node->port);
		cJSON_AddNumberToObject(js_node,"node_status", node->status);
	}
	return js_node_list;
}

//pack store->slots[SWARMKV_DEFAULT_SLOT_NUM] into msg
cJSON *swarmkv_msg_pack_slot_list(struct swarmkv_store *store)
{
	cJSON* js_slot_list = 0;
	cJSON* js_slot = 0;
	int i = 0;

	js_slot_list = cJSON_CreateArray();
	for(i=0;i<3;i++)
	{
		cJSON_AddItemToArray(js_slot_list, js_slot = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_slot,"slot_id", store->slots[i].slot_id);
		cJSON_AddNumberToObject(js_slot,"owner_id", store->slots[i].owner_node_id);
	}
	
	//cJSON_Delete(js_slot);
	return js_slot_list;
}

//check buff_size
int buff_check(size_t buff_size)
{
	if(buff_size < 0)
	{
		//printf("buff_size < 0.\n");
		return -1;
	}
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

//pack meet_req msg
size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}
	
	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_MEET_REQ;

	js_msg = swarmkv_msg_pack_basic_info(store, type);
	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);
	
	//printf("meet msg: %s \n",msg);
	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);

	cJSON_Delete(js_msg);
	return msg_len;
}

//ping, pong, meet_resp msg
size_t swarmkv_msg_pack_health_check(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}
		
	cJSON* js_msg = 0;
	cJSON* js_node_list = 0;
	cJSON* js_slot_list = 0;
	size_t msg_len = 0;
	char* msg = NULL;
	
	js_msg = swarmkv_msg_pack_basic_info(store, type);

	//js_node_list = cJSON_CreateArray();
	js_node_list = swarmkv_msg_pack_node_list(store);
	cJSON_AddItemToObject(js_msg,"node_list", js_node_list);

	//js_slot_list = cJSON_CreateArray();
	js_slot_list = swarmkv_msg_pack_slot_list(store);
	cJSON_AddItemToObject(js_msg,"slot_list", js_slot_list);
	
	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);

	//printf(" %s \n",msg);
	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);

	cJSON_Delete(js_msg);
	return msg_len;
}

//pack fail req msg
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_FAIL_REQ;

	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddNumberToObject(js_msg, "fail_node_id", fail_node_id);

	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);
	
	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);
	
	cJSON_Delete(js_msg);
    
	return msg_len;
}

size_t swarmkv_msg_pack_fail_response(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id, int op_result)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = NULL;
	char* msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_FAIL_RESP;
	
	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddNumberToObject(js_msg, "fail_node_id", fail_node_id);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);

	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);
	
	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);
	
	cJSON_Delete(js_msg);

	return msg_len;	
}

//put resp, invalidate resp
size_t swarmkv_msg_pack_update_key_response(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, 
											const char* tb_name, const char* key, const char* value, int op_result)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;
	
	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddStringToObject(js_msg, "table_name", tb_name);
	cJSON_AddStringToObject(js_msg, "key", key);
	cJSON_AddStringToObject(js_msg, "value", value);
	cJSON_AddNumberToObject(js_msg, "op_result", op_result);

	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);

	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);

	cJSON_Delete(js_msg);

	return msg_len;	
}

//pack put/invalidate key request msg
size_t swarmkv_msg_pack_update_key_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, 
					size_t buff_size, const char* tb_name, const char* key, const char* value)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;

	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddStringToObject(js_msg, "table_name", tb_name);
	cJSON_AddStringToObject(js_msg,"key", key);
	cJSON_AddStringToObject(js_msg, "value", value);

	msg = cJSON_PrintUnformatted(js_msg);
	if(msg)
	{
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
	cJSON_Delete(js_msg);

	return msg_len;
}

//pack get req msg
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, long unsigned int callback_addr)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_GET_REQ;

	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddNumberToObject(js_msg, "callback", callback_addr);
	cJSON_AddStringToObject(js_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_msg, "key", key);

	msg = cJSON_PrintUnformatted(js_msg);
	assert(msg!=NULL);
	
	msg_len = min(buff_size, strlen(msg));
	memcpy(buff, msg, msg_len);
	free(msg);

	cJSON_Delete(js_msg);

	return msg_len;
}

//pack get resp msg
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, char* value, long unsigned int callback_addr, char *uuid_version)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = NULL;
	char* msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_GET_RESP;

	js_msg = swarmkv_msg_pack_basic_info(store, type);
	
	cJSON_AddNumberToObject(js_msg, "callback", callback_addr);
	cJSON_AddStringToObject(js_msg, "uuid_version", uuid_version);
	cJSON_AddStringToObject(js_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_msg, "key", key);
	cJSON_AddStringToObject(js_msg, "value", value);

	msg = cJSON_PrintUnformatted(js_msg);
	if(msg)
	{
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
	cJSON_Delete(js_msg);

	return msg_len;
}

//unpack received message
int swarmkv_js_msg_unpack(char *buf, struct swarmkv_msg *msg)
{
	cJSON* js_msg = cJSON_Parse(buf);
	if(!js_msg) {
    	printf("get msg faild !\n");
		return -1;
	}
	
	cJSON* src_id = cJSON_GetObjectItem(js_msg, "my_id");
	cJSON* src_ip = cJSON_GetObjectItem(js_msg, "my_ip");
	cJSON* src_port = cJSON_GetObjectItem(js_msg, "my_port");
	cJSON* src_uuid = cJSON_GetObjectItem(js_msg, "uuid");
	cJSON* msg_type = cJSON_GetObjectItem(js_msg, "msg_type");
	msg->src_node_id = src_id->valueint;

	msg->src_ip = ALLOC(char, strlen(src_ip->valuestring));
	memcpy(msg->src_ip, src_ip->valuestring, strlen(src_ip->valuestring));
	msg->src_port = src_port->valueint;
	memcpy(msg->src_uuid, src_uuid->valuestring, 32);
	msg->type = msg_type->valueint;

	printf("msg->src_ip: %s\n", msg->src_ip);

	cJSON *tb_name = NULL;
	cJSON *key = NULL;
	cJSON *value = NULL;
	cJSON *cb_function = NULL;
	cJSON* node_list = NULL;
	cJSON* slot_list = NULL;
	cJSON *failed_node_id = NULL;
	cJSON *op_result = NULL;
	cJSON *last_uuid = NULL;
	int node_num = 0;
	int slot_num = 0;
	int tb_name_len = 0;
	int key_len = 0;
	int val_len = 0;

	switch(msg->type)
	{
		case MSG_PING:
		case MSG_PONG:
		case MSG_MEET_RESP:
			node_list = cJSON_GetObjectItem(js_msg, "node_list");
			slot_list = cJSON_GetObjectItem(js_msg, "slot_list");
			node_num = cJSON_GetArraySize(node_list);
			msg->data.health_check_data.node_num = node_num;
			slot_num = cJSON_GetArraySize(slot_list);
			msg->data.health_check_data.slot_num = slot_num;
			
			int i = 0;
			if(node_list != NULL){
				cJSON *node_item  = node_list->child;
				while(node_item)
				{ 
					cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
					cJSON *node_status = cJSON_GetObjectItem(node_item, "node_status");
					cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
					cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");	
					msg->data.health_check_data.nodes[i].node_id = node_id->valueint;
					int ip_len = strlen(node_ip->valuestring);
					msg->data.health_check_data.nodes[i].ip = ALLOC(char, ip_len);
					memcpy(msg->data.health_check_data.nodes[i].ip, node_ip->valuestring, ip_len);
					//msg->data.health_check_data.nodes[i].ip = node_ip->valuestring;
					msg->data.health_check_data.nodes[i].port = node_port->valueint;
					msg->data.health_check_data.nodes[i].status = node_status->valueint;
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
					cJSON *slot_id = cJSON_GetObjectItem(slot_item, "slot_id");
					cJSON *owner_id = cJSON_GetObjectItem(slot_item, "owner_id");
					msg->data.health_check_data.slots[i].slot_id = slot_id->valueint;
					
					msg->data.health_check_data.slots[i].owner_node_id = owner_id->valueint;
					i++;
					slot_item = slot_item->next;
				}
			}
			break;
		case MSG_MEET_REQ:
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
		case MSG_PUT_REQ:
		case MSG_INVALIDATE_KEYS_REQ:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "table_name");
			value = cJSON_GetObjectItem(js_msg, "value");
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.update_req_data.table_name = ALLOC(char, tb_name_len);
			msg->data.update_req_data.key = ALLOC(char, key_len);
			msg->data.update_req_data.value = ALLOC(char, val_len);
			memcpy(msg->data.update_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.update_req_data.key, key->valuestring, key_len);
			memcpy(msg->data.update_req_data.value, value->valuestring, val_len);
			break;
		case MSG_PUT_RESP:
		case MSG_INVALIDATE_KEYS_RESP:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "table_name");
			value = cJSON_GetObjectItem(js_msg, "value");
			op_result = cJSON_GetObjectItem(js_msg, "op_result");
			/*msg->data.update_resp_data.table_name = tb_name->valuestring;
			msg->data.update_resp_data.key = key->valuestring;
			msg->data.update_resp_data.value = value->valuestring;*/
			msg->data.update_resp_data.op_result = op_result->valueint;
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.update_resp_data.table_name = ALLOC(char, tb_name_len);
			msg->data.update_resp_data.key = ALLOC(char, key_len);
			msg->data.update_resp_data.value = ALLOC(char, val_len);
			memcpy(msg->data.update_resp_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.update_resp_data.key, key->valuestring, key_len);
			memcpy(msg->data.update_resp_data.value, value->valuestring, val_len);
			break;
		case MSG_GET_REQ:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			cb_function = cJSON_GetObjectItem(js_msg, "callback");
			//msg->data.get_req_data.table_name = tb_name->valuestring;
			//msg->data.get_req_data.key = key->valuestring;
			msg->data.get_req_data.cb_function = cb_function->valueint;
			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			msg->data.get_req_data.table_name = ALLOC(char, tb_name_len);
			msg->data.get_req_data.key = ALLOC(char, key_len);
			memcpy(msg->data.get_req_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.get_req_data.key, key->valuestring, key_len);
			break;
		case MSG_GET_RESP:
			tb_name = cJSON_GetObjectItem(js_msg, "table_name");
			key = cJSON_GetObjectItem(js_msg, "key");
			value = cJSON_GetObjectItem(js_msg, "value");
			cb_function = cJSON_GetObjectItem(js_msg, "callback");
			last_uuid = cJSON_GetObjectItem(js_msg, "last_uuid");
			/*msg->data.get_resp_data.table_name = tb_name->valuestring;
			msg->data.get_resp_data.key = key->valuestring;
			msg->data.get_resp_data.value = value->valuestring;*/
			memcpy(msg->data.get_resp_data.last_uuid, last_uuid->valuestring, 32);
			msg->data.get_resp_data.cb_function = cb_function->valueint;

			tb_name_len = strlen(tb_name->valuestring);
			key_len = strlen(key->valuestring);
			val_len = strlen(value->valuestring);
			msg->data.get_resp_data.table_name = ALLOC(char, tb_name_len);
			msg->data.get_resp_data.key = ALLOC(char, key_len);
			msg->data.get_resp_data.value = ALLOC(char, val_len);
			memcpy(msg->data.get_resp_data.table_name, tb_name->valuestring, tb_name_len);
			memcpy(msg->data.get_resp_data.key, key->valuestring, key_len);
			memcpy(msg->data.get_resp_data.value, value->valuestring, val_len);
			break;	
		case MSG_MIGRATE_REQ:
			break;
		case MSG_MIGRATE_RESP:	
			break;
		default:
			printf("UNKNOWN msg\n");
	}
	cJSON_Delete(js_msg);
	js_msg = NULL;

	return 1;
}