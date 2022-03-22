#pragma once
#include "sds.h"
#include "swarmkv_limits.h"

#include <netinet/ip.h>
#include <uuid/uuid.h>

struct swarmkv_node
{
//	uuid_t bin_uuid;
	char ip_addr[INET6_ADDRSTRLEN];	//#define INET6_ADDRSTRLEN 46 (#include <arpa/inet.h>, see man inet_ntop
	unsigned int p2p_port;	//host order
};
struct swarmkv_options
{
	unsigned int health_check_port;
	unsigned int p2p_port;
	int minimum_replica_num;
	int dryrun;
	int disable_cache;
	unsigned int consul_port;
	void *MESA_handle_logger;
	int loglevel;
	char bind_address[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	size_t nr_worker_threads;
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

	SWARMKV_OP_KEYSPACE_SET,		//KEYSPACE SET key host port (of object owner)
	SWARMKV_OP_KEYSPACE_SETNXGET,	//KEYSPACE SETNXGET key host port (of object owner)
	SWARMKV_OP_KEYSPACE_GET,		//KEYSPACE GET key
	SWARMKV_OP_KEYSPACE_DEL,		//KEYSPACE DEL key
	SWARMKV_OP_KEYSPACE_GETSLOTKEYS,//KEYSPACE GETSLOTKYES slot_id
	SWARMKV_OP_KEYSPACE_SETSLOT,	//KEYSPACE SETSLOT slot_id IMPORTING host port|MIGRATING host port|NODE host port|STABLE

	SWARMKV_OP_CLUSTER_SYNC,		//CLUSTER SYNC object_version $POPULATE_COMMAND
	__SWARMKV_OP_MAX
};

struct swarmkv_cmd
{
	enum swarmkv_operator op;
	size_t n_args;
	sds *args;
};
void swarmkv_common_global_init(void);
void swarmkv_common_global_deinit(void);

const char* swarmkv_cmd_op2str(enum swarmkv_operator op);
enum swarmkv_operator swarmkv_cmd_str2op(const char* opstr, size_t len);
int has_sub_operator(const char* op_str);
int command_str2op(const char* op_str, const char *sub_op_str, enum swarmkv_operator *op);


struct swarmkv_cmd* swarmkv_cmd_new(enum swarmkv_operator op, size_t n_args);
void swarmkv_cmd_free(struct swarmkv_cmd* p);
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin);

struct swarmkv_reply* swarmkv_reply_new_string(const char* str, size_t sz);
struct swarmkv_reply* swarmkv_reply_new_integer(long long integer);
struct swarmkv_reply* swarmkv_reply_new_array(size_t n_element);
struct swarmkv_reply* swarmkv_reply_new_nil(void);
struct swarmkv_reply* swarmkv_reply_new_status(const char *format, ...);
struct swarmkv_reply* swarmkv_reply_new_error(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_dup(const struct swarmkv_reply *origin);

void node2reply(const struct swarmkv_node *node, struct swarmkv_reply* reply[2]);
void sds2node(const sds args[2], struct swarmkv_node *node);
int is_store_read_operator(enum swarmkv_operator op);
int is_keyspace_operator(enum swarmkv_operator op);
int is_store_del_operator(enum swarmkv_operator op);

