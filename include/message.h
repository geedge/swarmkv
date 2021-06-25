#include <stdio.h>
#include "swarmkv.c"

enum swarmkv_msg_type
{
	MSG_HEALTH_CHECK_PING,
    MSG_HEALTH_CHECK_PONG,
    MSG_FAILED,
    MSG_MEET_REQ,
    MSG_MEET_RESP,
	MSG_MOVE_REQ,    
    MSG_MOVE_RESP,
    MSG_PUT_REQ,
	MSG_PUT_RESP,
	MSG_GET_REQ,
	MSG_GET_RESP,
	MSG_INVALIDATE_KEY,
};
struct swarmkv_msg
{
	swarmkv_msg_type msg_type;   
    //int totlen;      //message total length
	int src_node_id;
    uint32_t src_ip;
	int *dst_node_id;
	int dst_node_num;
	char uuid[32];
	char *data;
	size_t datasize;
};

struct swarmkv_HEALTH_CHECK_PING
{
    struct swarmkv_node nodes;
	//struct swarmkv_slot myslots[65535];
};
struct swarmkv_HEALTH_CHECK_PONG
{
    struct swarmkv_node nodes;
    //struct swarmkv_slot myslots[65535];
};
struct swarmkv_FAILED
{
	int failed_node_id;    //broadcast
};
struct swarmkv_MEET_REQ
{
	//
};
struct swarmkv_MEET_RESP
{
	struct swarmkv_slot slots[65535];
};

struct swarmkv_MOVE_REQ
{
	int* slot_id;
    int slot_num;
};
struct swarmkv_MOVE_RESP
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
	int result;
};

struct swarmkv_GET
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

struct swarmkv_INVALIDATE_KEY
{
	char* key;
	size_t keylen;
};



