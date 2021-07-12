#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

#include "swarmkv_internal.h"

#define SWARMKV_MAX_MSG_SIZE 65500
#define OP_SUCCESS 1
#define OP_UNSUCCESS -1

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

//ping, pong, meet resp
struct swarmkv_health_check
{
	struct swarmkv_node *nodes;
	struct swarmkv_slot myslots[SWARMKV_DEFAULT_SLOT_NUM];
};

struct swarmkv_fail_req
{
	int failed_node_id;
};

struct swarmkv_fail_resp
{
	int failed_node_id;
	int op_result;
};

struct swarmkv_update_req
{
	char *table;
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
};

struct swarmkv_update_resp
{
	char *table;
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
	int op_result;
};

struct swarmkv_get_req
{
	char *table;
	char *key;
	size_t keylen;
};

struct swarmkv_get_resp
{
	char *table;
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
};

struct swarmkv_migrate_req
{
	int slot_id;
};

struct swarmkv_migrate_resp
{
	int slot_id;
	struct swarmkv_table *tables;
};

union MsgData
{
	struct swarmkv_health_check health_check_data;
	struct swarmkv_fail_req fail_req_data;
	struct swarmkv_fail_resp fail_resp_data;
	struct swarmkv_update_req update_req_data;
	struct swarmkv_update_resp update_resp_data;
	struct swarmkv_get_req get_req_data;
	struct swarmkv_get_resp get_resp_data;
	struct swarmkv_migrate_req migrate_req_data;
	struct swarmkv_migrate_resp migrate_resp_data;
};

struct swarmkv_msg
{
	int src_node_id;
	char *src_ip;
	uint16_t src_port;
	char uuid[32];
	enum swarmkv_msg_type type;
	union MsgData data;
};

cJSON *swarmkv_msg_pack_basic_info(struct swarmkv_store *store, enum swarmkv_msg_type type);
cJSON *swarmkv_msg_pack_node_list(struct swarmkv_store *store);
cJSON *swarmkv_msg_pack_slot_list(struct swarmkv_store *store);

size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_health_check(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id);
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char *key,
								long unsigned int cb_function);
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char *key,
								 char *value, long unsigned int cb_function, char *uuid_version);
size_t swarmkv_msg_pack_ok_response(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_update_key_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
									   const char *tb_name, const char *key, const char *value);

struct swarmkv_msg* unpack();