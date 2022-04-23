#pragma once
#include "sds.h"
#include "swarmkv_limits.h"

#include <netinet/ip.h>
#include <uuid/uuid.h>
#include <MESA/MESA_handle_logger.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>

enum cmd_fastpath_result
{
	NEED_KEY_ROUTE,
	REDIRECT,
	FINISHED
};
struct swarmkv_module
{
	char name[SWARMKV_SYMBOL_MAX];
	pthread_rwlock_t rwlock;
	void *mod_ctx;
};

struct swarmkv_node_addr
{
//	uuid_t bin_uuid;
	char ip_addr[INET6_ADDRSTRLEN];	//#define INET6_ADDRSTRLEN 46 (#include <arpa/inet.h>, see man inet_ntop
	unsigned int p2p_port;	//host order
};
struct swarmkv_node
{	
	struct swarmkv_node_addr addr;
	uuid_t bin_uuid;
};
enum slot_state
{
	STATE_STABLE=0,
	STATE_MIGRATING,
	STATE_IMPORTING
};

struct key_slot
{
	int slot_id;
	enum slot_state state;
	struct swarmkv_node owner;
	struct swarmkv_node peer;//peer is used for state MIGRATING and IMPORTING
//	struct swarmkv_node_addr replica[MAX_REPLICA_NUM];
	struct key_route_entry* keyroute_table;
	int I_am_owner;
	pthread_mutex_t mutex;
};
void json2keyslots(const char *json_buffer, struct key_slot slots[], size_t n_slot);
sds keyslots2json(struct key_slot slots[], size_t n_slot);
void json2nodes(const char* resp_buff, struct swarmkv_node *nodes, size_t *n_node);
void json2leader(const char *resp_body, struct swarmkv_node *leader);
int get_slot_id(const char* key, size_t keylen);

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

struct swarmkv_cmd
{
//	enum swarmkv_operator op;
	size_t argc;
	sds *argv;
};

struct swarmkv_cmd* swarmkv_cmd_new(size_t argc);
void swarmkv_cmd_free(struct swarmkv_cmd* p);
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin);

struct swarmkv_reply *swarmkv_reply_new_string(const char* str, size_t sz);
struct swarmkv_reply *swarmkv_reply_new_string_fmt(const char *format, ...);

struct swarmkv_reply *swarmkv_reply_new_integer(long long integer);
struct swarmkv_reply *swarmkv_reply_new_array(size_t n_element);
struct swarmkv_reply *swarmkv_reply_new_nil(void);
struct swarmkv_reply *swarmkv_reply_new_status(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_new_error(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_dup(const struct swarmkv_reply *origin);
void swarmkv_reply_merge_array(struct swarmkv_reply **dst, struct swarmkv_reply *src);

void node_addr2reply(const struct swarmkv_node_addr *node, struct swarmkv_reply* reply[2]);
void sds2node_addr(const sds args[2], struct swarmkv_node_addr *node);
void node_addr2sds(const struct swarmkv_node_addr *node, sds argv[2]);

int swarmkv_node_compare(const struct swarmkv_node *node1, const struct swarmkv_node *node2);
void swarmkv_node_copy(struct swarmkv_node *dst, const struct swarmkv_node *src);
int swarmkv_node_addr_compare(const struct swarmkv_node_addr *addr1, const struct swarmkv_node_addr *addr2);
void swarmkv_node_addr_copy(struct swarmkv_node_addr *dst, const struct swarmkv_node_addr *src);

int http_blocking_request(enum evhttp_cmd_type type, const char* host, unsigned int port, const char* url, sds req_body, sds *resp_body);



