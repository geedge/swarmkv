#include "./inc_internal/swarmkv_message.h"

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
	cJSON_Delete(js_node);
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
	cJSON_Delete(js_slot);
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
	
	cJSON* js_meet_msg = 0;
	char* meet_msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_MEET_REQ;

	js_meet_msg = swarmkv_msg_pack_basic_info(store, type);
	meet_msg = cJSON_PrintUnformatted(js_meet_msg);
	if(meet_msg)
	{
		msg_len = min(buff_size, strlen(meet_msg));
		memcpy(buff, meet_msg, msg_len);
		free(meet_msg);
	}
	cJSON_Delete(js_meet_msg);
	return msg_len;
}

//ping, pong, meet_resp msg
size_t swarmkv_msg_pack_health_check(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}
		
	cJSON* js_health_check_msg = 0;
	cJSON* js_node_list = 0;
	cJSON* js_slot_list = 0;
	size_t msg_len = 0;
	char* health_check_msg = NULL;
	
	js_health_check_msg = swarmkv_msg_pack_basic_info(store, type);

	js_node_list = swarmkv_msg_pack_node_list(store);
	cJSON_AddItemToObject(js_health_check_msg,"node_list", js_node_list);

	js_slot_list = swarmkv_msg_pack_slot_list(store);
	cJSON_AddItemToObject(js_health_check_msg,"slot_list", js_slot_list);
	
	health_check_msg = cJSON_PrintUnformatted(js_health_check_msg);

	if(health_check_msg)
	{
		msg_len = min(buff_size, strlen(health_check_msg));
		memcpy(buff, health_check_msg, msg_len);
		free(health_check_msg);
	}

	cJSON_Delete(js_health_check_msg);
	cJSON_Delete(js_node_list);
	cJSON_Delete(js_slot_list);

	return msg_len;
}

//pack fail req msg
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_fail_msg = 0;
	char* fail_msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_FAIL_REQ;

	js_fail_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddNumberToObject(js_fail_msg, "fail_node_id", fail_node_id);

	fail_msg = cJSON_PrintUnformatted(js_fail_msg);
	if(fail_msg)
	{
		msg_len = min(buff_size, strlen(fail_msg));
		memcpy(buff, fail_msg, msg_len);
		free(fail_msg);
	}
	cJSON_Delete(js_fail_msg);
    
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

	cJSON* js_resp_msg = 0;
	char* resp_msg = NULL;
	size_t msg_len = 0;
	
	js_resp_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddStringToObject(js_resp_msg, "msg", "ok");

	resp_msg = cJSON_PrintUnformatted(js_resp_msg);
	if(resp_msg)
	{
		msg_len = min(buff_size, strlen(resp_msg));
		memcpy(buff, resp_msg, msg_len);
		free(resp_msg);
	}
	cJSON_Delete(js_resp_msg);

	return msg_len;	
}

//pack put/invalidate key request msg
size_t swarmkv_msg_pack_update_key_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, const char* tb_name, const char* key, const char* value)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_update_key_req = 0;
	char* update_key_req = NULL;
	size_t msg_len = 0;

	js_update_key_req = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddStringToObject(js_update_key_req, "table_name", tb_name);
	cJSON_AddStringToObject(js_update_key_req,"key", key);
	cJSON_AddStringToObject(js_update_key_req, "value", value);

	update_key_req = cJSON_PrintUnformatted(js_update_key_req);
	if(update_key_req)
	{
		msg_len = min(buff_size, strlen(update_key_req));
		memcpy(buff, update_key_req, msg_len);
		free(update_key_req);
	}
	cJSON_Delete(js_update_key_req);

	return msg_len;
}

//pack get req msg
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, long unsigned int callback_addr)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_get_req_msg = 0;
	char* get_req_msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_GET_REQ;

	js_get_req_msg = swarmkv_msg_pack_basic_info(store, type);
	cJSON_AddNumberToObject(js_get_req_msg, "callback", callback_addr);
	cJSON_AddStringToObject(js_get_req_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_get_req_msg, "key", key);

	get_req_msg = cJSON_PrintUnformatted(js_get_req_msg);
	if(get_req_msg)
	{
		msg_len = min(buff_size, strlen(get_req_msg));
		memcpy(buff, get_req_msg, msg_len);
		free(get_req_msg);
	}
	cJSON_Delete(js_get_req_msg);

	return msg_len;
}

//pack get resp msg
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, char* value, long unsigned int callback_addr)
{
	if(buff_check(buff_size) <= 0)
	{
		return -1;
	}

	cJSON* js_get_resp_msg = 0;
	char* get_resp_msg = NULL;
	size_t msg_len = 0;
	enum swarmkv_msg_type type = MSG_GET_RESP;

	js_get_resp_msg = swarmkv_msg_pack_basic_info(store, type);
	
	cJSON_AddNumberToObject(js_get_resp_msg, "callback", callback_addr);
	cJSON_AddStringToObject(js_get_resp_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_get_resp_msg, "key", key);
	cJSON_AddStringToObject(js_get_resp_msg, "value", value);

	get_resp_msg = cJSON_PrintUnformatted(js_get_resp_msg);
	if(get_resp_msg)
	{
		msg_len = min(buff_size, strlen(get_resp_msg));
		memcpy(buff, get_resp_msg, msg_len);
		free(get_resp_msg);
	}
	cJSON_Delete(js_get_resp_msg);

	return msg_len;
}

/*
size_t swarmkv_msg_pack(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, char* tb_name, char* key, char* value, int fail_node_id, char* calloback)
{
	if(buff_size < 0)
	{
		printf("buff_size < 0.\n");
		return -1;
	}
	if(buff_size == 0)
	{
		return 0;
	}

	cJSON* js_msg = 0;
	char *msg = ALLOC(char, SWARMKV_MAX_MSG_SIZE);
	js_msg = swarmkv_msg_pack_basic_info(store, type);

	switch(type)
	{	
		case MSG_PING:
		case MSG_PONG:
		case MSG_MEET_RESP:
			cJSON* js_node_list = 0;
			cJSON* js_slot_list = 0;
			js_node_list = swarmkv_msg_pack_node_list(store);
			cJSON_AddItemToObject(js_msg,"node_list", js_node_list);
			js_slot_list = swarmkv_msg_pack_slot_list(store);
			cJSON_AddItemToObject(js_msg,"slot_list", js_slot_list);
			cJSON_Delete(js_node_list);
			cJSON_Delete(js_slot_list);
			break;

		case MSG_MEET_REQ:
			msg = cJSON_PrintUnformatted(js_meet_msg);
			break;

		case MSG_FAIL_REQ:
			if(fail_node_id<0)
			{
				printf("please set a fail node id.\n");
				break;
			}
			cJSON_AddNumberToObject(js_msg, "fail_node_id", fail_node_id);
			break;

		case MSG_FAIL_RESP:
		case MSG_PUT_RESP:
		case MSG_INVALIDATE_KEYS_RESP:
			//only response "ok"
			cJSON_AddStringToObject(js_msg, "msg", "ok");
			break;

		case MSG_PUT_REQ:	
		case MSG_INVALIDATE_KEYS_REQ:
			if(tb_name && key && value)
			{
				cJSON_AddStringToObject(js_msg, "table_name", tb_name);
				cJSON_AddStringToObject(js_msg,"key", key);
				cJSON_AddStringToObject(js_msg, "value", value);
			}
			else
			{
				printf("Missing parameters.\n");
			}
			break;

		case MSG_GET_REQ:
			cJSON_AddStringToObject(js_msg, "callback", cb_function);
			cJSON_AddStringToObject(js_msg, "table_name", table_name);
			cJSON_AddStringToObject(js_msg, "key", key);
			break;

		case MSG_GET_RESP:
			cJSON_AddStringToObject(js_msg, "callback", cb_function);
			cJSON_AddStringToObject(js_msg, "table_name", table_name);
			cJSON_AddStringToObject(js_msg, "key", key);
			cJSON_AddStringToObject(js_msg, "value", value);
			break;

		case MSG_MIGRATE_REQ:   
		case MSG_MIGRATE_RESP:
			break;
		
		default:
			printf("UNKNOWN message type!\n");
	}
	msg = cJSON_PrintUnformatted(js_msg);
	size_t msg_len = strlen(msg);
	printf("msg_len: %d\n", msg_len);

	if(msg_len > buff_size)
	{
		memcpy(buff, msg, buff_size);
	}
	else
	{
		memcpy(buff, msg, msg_len);
	}
	cJSON_Delete(js_msg);
	free(msg);
	msg = NULL;

	return msg_len;
}
*/