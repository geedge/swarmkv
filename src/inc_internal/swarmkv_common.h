#pragma once
#include "sds.h"
#include "swarmkv_limits.h"

#include <netinet/ip.h>
#include <uuid/uuid.h>

struct swarmkv_node
{
//	char uuid[MAX_UUID_STR_LEN];
	uuid_t bin_uuid;
	char ip_addr[INET6_ADDRSTRLEN];	//#define INET6_ADDRSTRLEN 46 (#include <arpa/inet.h>, see man inet_ntop
	unsigned int p2p_port;	//host order
};

enum swarmkv_operator
{
	SWARMKV_OP_GET,	//GET key, return value
	SWARMKV_OP_SET,	//SET key value
	SWARMKV_OP_DEL,	//DEL key
	SWARMKV_OP_HSET,	//HSET key field value
	SWARMKV_OP_HGET,	//HGET key field
	SWARMKV_OP_HGETALL,	//HGETALL key, returen all fields
	SWARMKV_OP_HDEL,	//HDEL key field
	SWARMKV_OP_INCRBY,	//INCRBY key number
	SWARMKV_OP_DECRBY,	//OP_DECRBY key number
	SWARMKV_OP_THROTTLESET,	//THROTTLESET .. throttle
	SWARMKV_OP_EXPIRE,	//EXPIRE key seconds
	SWARMKV_OP_CLUSTER_OBJ_OWNER,	//CLUSTER OBJ_OWNER key [NX]
	__SWARMKV_CMD_MAX
};

struct swarmkv_cmd
{
	enum swarmkv_operator op;
	size_t n_args;
	sds args[MAX_CMD_ARGS];
};

struct swarmkv_cmd* swarmkv_cmd_new(enum swarmkv_operator op, size_t n_args);
void swarmkv_cmd_free(struct swarmkv_cmd* p);
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin);


