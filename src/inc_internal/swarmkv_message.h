#pragma once

#include <stdio.h>
#include <cjson/cJSON.h> 
#include "swarmkv_internal.h"

enum swarmkv_msg_type
{
	MSG_PING,
    MSG_PONG,
	MSG_MEET_REQ,
    MSG_MEET_RESP,
    MSG_FAIL_REQ,
	MSG_FAIL_RESP,
	MSG_MIGRATE_REQ,    
    MSG_MIGRATE_RESP,
    MSG_PUT_REQ,
	MSG_PUT_RESP,
	MSG_GET_REQ,
	MSG_GET_RESP,
	MSG_INVALIDATE_KEYS_REQ,
	MSG_INVALIDATE_KEYS_RESP,
};

#define SWARMKV_MAX_MSG_SIZE 65500

cJSON *swarmkv_msg_pack_basic_info(struct swarmkv_store *store, enum swarmkv_msg_type type);
cJSON *swarmkv_msg_pack_node_list(struct swarmkv_store *store);
cJSON *swarmkv_msg_pack_slot_list(struct swarmkv_store *store);

size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_health_check(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id);
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, long unsigned int cb_function);
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, char* value, long unsigned int cb_function);
size_t swarmkv_msg_pack_ok_response(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_update_key_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, const char* tb_name, const char* key, const char* value);

//size_t swarmkv_msg_pack(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size, char* tb_name, char* key, char* value, int fail_node_id, char* cb_function);


