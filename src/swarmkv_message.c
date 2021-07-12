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
	//cJSON_Delete(js_node);
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
	if(msg)
	{
		printf("meet msg: %s \n",msg);
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
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
	if(msg)
	{
		printf(" %s \n",msg);
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
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
	if(msg)
	{
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
	cJSON_Delete(js_msg);
    
	return msg_len;
}

//pack "ok" response msg
//put resp, invalidate resp, fail resp
size_t swarmkv_msg_pack_ok_response(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_msg = 0;
	char* msg = NULL;
	size_t msg_len = 0;
	
	js_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddStringToObject(js_msg, "msg", "ok");

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

//pack put/invalidate key request msg
size_t swarmkv_msg_pack_update_key_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, const char* tb_name, const char* key, const char* value)
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
	if(msg)
	{
		msg_len = min(buff_size, strlen(msg));
		memcpy(buff, msg, msg_len);
		free(msg);
	}
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

	cJSON* js_msg = 0;
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

struct swarmkv_msg* unpack()
{
	
}