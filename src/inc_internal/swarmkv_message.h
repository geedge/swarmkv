#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

#include "swarmkv_internal.h"

//UDP数据包的最大理论长度是 65535 - 8 - 20 = 65507B,UDP包头有16bit的数据包长度字段，所以是2^16
//但是最大MTU是1500, UDP_len = 1500-20-8 = 1472B
//Internet上的标准MTU大多是576B，UDP_len=576-20-8=548B
//超过MTU的UDP包在IP层会分片然后重组，如果分片丢失就会直接丢弃这个UDP数据包
//考虑MTU TCP数据包的大小，TCP_len=1500-20-20 = 1460，不考虑则无限制
#define SWARMKV_MAX_MSG_SIZE 65507

#define OP_UNSUCCESS -1
#define REQ_NODE_ERROR 0      //I am not the owner of the key
#define OP_SUCCESS 1
#define MEET_RECIEVE 2
#define MEET_REJECT 3
#define MEET_ID_CONFLICT 2

#define SWARMKV_DEFAULT_TTL 1

enum swarmkv_msg_type
{
	MSG_PING,                      //0
	MSG_PONG,                      //1
	MSG_MEET_REQ,                  //2
	MSG_MEET_RESP,                 //3
	MSG_FAIL_REQ,                  //4
	MSG_FAIL_RESP,                 //5
	MSG_MIGRATE_REQ,               //6
	MSG_MIGRATE_RESP,              //7
	MSG_MIGRATE_MOVED,             //8
	MSG_DEL_KV,                    //9
	MSG_SLOT_OWNER_CHANGE,         //10
	MSG_PUT_REQ,                   //11
	MSG_PUT_RESP,                  //12
	MSG_GET_REQ,                   //13
	MSG_GET_RESP,                  //14   
	MSG_INVALIDATE_KEYS_REQ,       //15
	MSG_INVALIDATE_KEYS_RESP,      //16
	MSG_JOIN_COMPPLETE,            //17
};


struct node
{
	int node_id;       //Key
	char* ip;
	uint16_t port;
	int status;        //ONLINE/PFAIL/FAIL
};

//ping, pong, meet resp
//struct swarmkv_health_check
struct swarmkv_basic_infomation
{
	int node_num;
	struct node nodes[SWARMKV_DEFAULT_NODE_NUM];
	int slot_num;
	struct swarmkv_slot slots[SWARMKV_DEFAULT_SLOT_NUM];
};

struct swarmkv_meet_resp
{
	int op_result;
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
	char uuid_version[37];
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
	char uuid_version[37];
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

//因为decode过程可以算出来长度，所以不用把长度也带上
struct swarmkv_del_req
{
	char *table_name;
	char *key;
	char *value;
};

struct slot_moved
{
	int slot_id;
	int new_owner_id;
};

struct migrate_moved
{
	int slot_id;
	int new_owner_id;
};

union MsgData
{
	struct swarmkv_basic_infomation swarmkv_basic_info;
	struct swarmkv_meet_resp meet_resp_data;
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
	struct swarmkv_del_req del_req_data;
	struct slot_moved slot_moved_data;
	struct migrate_moved migrate_moved_data;
};

struct swarmkv_msg
{
	int src_node_id;
	char *src_ip;
	uint16_t src_port;
	char src_uuid[37];
	enum swarmkv_msg_type type;
	int ttl;
	union MsgData data;
};


size_t swarmkv_msg_pack_ping(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_pong(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_meet_req(struct swarmkv_store *store, char *buff, size_t buff_size);
//size_t swarmkv_msg_pack_meet_resp(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_meet_resp(struct swarmkv_store *store, char *buff, size_t buff_size, int op_result);
size_t swarmkv_msg_pack_fail_req(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id);
size_t swarmkv_msg_pack_fail_response(struct swarmkv_store *store, char *buff, size_t buff_size, int fail_node_id, int op_result);
size_t swarmkv_msg_pack_del_msg(struct swarmkv_store *store, char *buff, size_t buff_size,
					 const char* tb_name, const char* key, const char* value);

//get
size_t swarmkv_msg_pack_get_req(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key);
size_t swarmkv_msg_pack_get_req_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);
size_t swarmkv_msg_pack_get_resp(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, 
									const char* key, const char* value, int op_result);
size_t swarmkv_msg_pack_get_resp_with_cb(struct swarmkv_store *store, char *buff, size_t buff_size, const char *table_name, const char* key, 
									const char* value, int op_result, const char* uuid_version, long unsigned int callback_func_addr, long unsigned int callback_arg_addr);

//put&invalidate
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

size_t swarmkv_msg_pack_slot_moved(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id, int new_owner_id);
size_t swarmkv_msg_pack_migrate_moved(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id, int new_owner_id);
size_t swarmkv_msg_pack_join_complete(struct swarmkv_store *store, char *buff, size_t buff_size);
size_t swarmkv_msg_pack_slot_owner_change(struct swarmkv_store *store, char *buff, size_t buff_size, int slot_id, int new_owner_id);


