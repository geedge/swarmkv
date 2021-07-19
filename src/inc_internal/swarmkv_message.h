#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

#include "swarmkv_internal.h"

#define SWARMKV_MAX_MSG_SIZE 65500
#define OP_SUCCESS 1
#define OP_UNSUCCESS -1
#define REQ_ERROR 0      //I am not the owner of the key
#define SWARMKV_DEFAULT_TTL 1

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


struct node
{
	int node_id;       //Key
	char* ip;
	uint16_t port;
	int status;        //ONLINE/PFAIL/FAIL
};

//ping, pong, meet resp
struct swarmkv_health_check
{
	int node_num;
	struct node nodes[SWARMKV_DEFAULT_NODE_NUM];
	int slot_num;
	struct swarmkv_slot slots[SWARMKV_DEFAULT_SLOT_NUM];
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

struct swarmkv_invalidate_req
{
	char *table_name;
	char *key;
	char *value;
};

struct swarmkv_invalidate_resp
{
	char *table_name;
	char *key;
	char *value;
	int op_result;
};

struct swarmkv_put_req
{
	char *table_name;
	char *key;
	char *value;
	long unsigned int cb_function; 
	long unsigned int cb_arg;
};

struct swarmkv_put_resp
{
	char *table_name;
	char *key;
	char *value;
	int op_result;
	char uuid_version[32];
	long unsigned int cb_function; 
	long unsigned int cb_arg;
};

struct swarmkv_get_req
{
	char *table_name;
	char *key;
	long unsigned int cb_function; 
	long unsigned int cb_arg;
};
struct swarmkv_get_resp
{
	char *table_name;
	char *key;
	char *value;
	int op_result;
	char uuid_version[32];
	long unsigned int cb_function; 
	long unsigned int cb_arg;
};

struct swarmkv_migrate_req
{
	int slot_id;
};

struct swarmkv_migrate_kv
{
	char *table_name;
	char *key;
	char *value;
};

struct swarmkv_migrate_resp
{
	int slot_id;
	int kv_num;
	struct swarmkv_migrate_kv *mkv;
};

union MsgData
{
	struct swarmkv_health_check health_check_data;
	struct swarmkv_fail_req fail_req_data;
	struct swarmkv_fail_resp fail_resp_data;
	struct swarmkv_put_req put_req_data;
	struct swarmkv_put_resp put_resp_data;
	struct swarmkv_get_req get_req_data;
	struct swarmkv_get_resp get_resp_data;
	struct swarmkv_invalidate_req invalidate_req_data;
	struct swarmkv_invalidate_resp invalidate_resp_data;
	struct swarmkv_migrate_req migrate_req_data;
	struct swarmkv_migrate_resp migrate_resp_data;
};

struct swarmkv_msg
{
	int src_node_id;
	char *src_ip;
	uint16_t src_port;
	char src_uuid[32];
	enum swarmkv_msg_type type;
	int ttl;
	union MsgData data;
};



cJSON *swarmkv_msg_pack_basic_info(struct swarmkv_store *store, enum swarmkv_msg_type type);
cJSON *swarmkv_msg_pack_node_list(struct swarmkv_store *store);
cJSON *swarmkv_msg_pack_slot_list(struct swarmkv_store *store);

size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_health_check(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id);
size_t swarmkv_msg_pack_fail_response(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id, int op_result);

//get
cJSON *swarmkv_msg_pack_get_basic_info(struct swarmkv_store *store, const char *table_name, const char* key);
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key);
size_t swarmkv_msg_pack_get_req_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, char* value, int op_result);
size_t swarmkv_msg_pack_get_resp_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, 
									const char* value, int op_result, const char* uuid_version, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);

//put&invalidate
cJSON *swarmkv_msg_pack_put_basic_info(struct swarmkv_store *store, enum swarmkv_msg_type type, 
											const char* tb_name, const char* key, const char* value);
size_t swarmkv_msg_pack_put_req(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value);
size_t swarmkv_msg_pack_put_req_with_cb(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);
size_t swarmkv_msg_pack_put_resp(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, int op_result);
size_t swarmkv_msg_pack_put_resp_with_cb(struct swarmkv_store *store, enum swarmkv_msg_type type, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value, int op_result, const char* uuid_version, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);

size_t swarmkv_msg_pack_migration_req(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id);
size_t swarmkv_msg_pack_migration_resp(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id,
										struct swarmkv_migrate_kv mkv[], int mkv_num);
size_t swarmkv_msg_repack_ttl(char *raw_msg, char *buff, size_t buff_size);
int swarmkv_js_msg_unpack(char *buf, struct swarmkv_msg* msg);