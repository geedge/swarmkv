#pragma once

#include <stdio.h>
#include "swarmkv.h"

enum swarmkv_msg_type
{
	MSG_PING,
    MSG_PONG,
	MSG_MEET_REQ,
    MSG_MEET_RESP,
	//MSG_PFAIL,
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

//ping&pong&meet_resp
struct swarmkv_health_check
{
	struct swarmkv_node *nodes;
	struct swarmkv_slot myslots[65535];
};

struct swarmkv_MEET_REQ
{
	struct swarmkv_node *nodes;
};

struct swarmkv_FAIL_REQ
{
	int failed_node_id;  
};

struct swarmkv_FAIL_RESP
{
	char* result;
};

struct swarmkv_MIGRATE_REQ
{
	int* slot_id;
    int slot_num;
};

struct swarmkv_MIGRATE_RESP
{
    struct swarmkv_value *kv;
    size_t kv_num;
};

struct swarmkv_PUT_REQ
{
	char* key;
	size_t keylen;
	char* value;
	size_t vallen;
};
struct swarmkv_PUT_RESP
{
	char* key;
	size_t keylen;
	char* result;
};

struct swarmkv_GET_REQ
{
	char* key;
	size_t keylen;
};

struct swarmkv_GET_RESP
{
	char* key;
	size_t keylen;
	char* value;
	size_t vallen;
};

struct swarmkv_INVALIDATE_KEYS_REQ
{
	char* key;
	size_t keylen;
};

struct swarmkv_INVALIDATE_KEYS_RESP
{
	char* result;
};

union MsgData {
    /* PING, MEET and PONG */
    struct swarmkv_health_check health_check_data;
	struct swarmkv_MEET_REQ meet_req_data;
	struct swarmkv_FAIL_REQ fail_req_data;
	struct swarmkv_FAIL_RESP fail_resp_data;
	struct swarmkv_MIGRATE_REQ migrate_req_data;
	struct swarmkv_MIGRATE_RESP migrate_resp_data;
	struct swarmkv_PUT_REQ put_req_data;
	struct swarmkv_PUT_RESP put_resp_data;
	struct swarmkv_GET_REQ get_req_data;
	struct swarmkv_GET_RESP get_resp_data;
	struct swarmkv_INVALIDATE_KEYS_REQ invalidate_keys_req_data;
	struct swarmkv_INVALIDATE_KEYS_RESP invalidate_keys_resp_data;
};

struct swarmkv_msg
{
	int src_node_id;
    uint32_t src_ip;
	uint16_t src_port;
	enum swarmkv_msg_type msg_type;
	union MsgData data;
};



