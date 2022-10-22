#pragma once
#include "sds.h"
#include "log.h"
#include "swarmkv_limits.h"
#include "swarmkv/swarmkv.h"
#include "future_promise.h"

#include <netinet/ip.h>
#include <uuid/uuid.h>
#include <MESA/MESA_handle_logger.h>

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>

enum cmd_exec_result
{
	NEED_KEY_ROUTE,
	REDIRECT,
	FINISHED
};
	
struct swarmkv_module;
typedef void swarmkv_module_lock_func_t(struct swarmkv_module *mod, sds key);

struct swarmkv_module
{
	char name[SWARMKV_SYMBOL_MAX];
	swarmkv_module_lock_func_t *lock;
	swarmkv_module_lock_func_t *unlock;
	void *mod_ctx;
};

struct swarmkv_node_addr
{
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
	struct swarmkv_node owner;
};
void json2keyslots(sds json_buffer, void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container);
sds keyslots2json(void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container);
void json2nodes(const char* resp_buff, struct swarmkv_node *nodes, size_t *n_node);
void json2leader(const char *resp_body, struct swarmkv_node *leader);
int key_hash_slot(const char* key, size_t keylen);

struct swarmkv_options
{
	unsigned int health_check_port;
	unsigned int p2p_port;
	int minimum_replica_num;
	int dryrun;
	int disable_cache;
	unsigned int consul_port;
    unsigned int p2p_timeout_us;
	unsigned int sync_interval_us;
	struct log_handle *logger;
	int loglevel;
	char bind_address[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	size_t nr_worker_threads;
	int is_assigned_to_db;
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
struct swarmkv_reply *swarmkv_reply_new_node(struct swarmkv_node_addr *addr, int is_ask);
struct swarmkv_reply *swarmkv_reply_new_nil(void);
struct swarmkv_reply *swarmkv_reply_new_status(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_new_error(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_clone(const struct swarmkv_reply *origin);
void swarmkv_reply_merge_array(struct swarmkv_reply **dst, struct swarmkv_reply *src);
void swarmkv_reply_append_string(struct swarmkv_reply **dst, struct swarmkv_reply *src);

int swarmkv_reply2node_addr(const struct swarmkv_reply *reply, struct swarmkv_node_addr *node);
int swarmkv_reply2nodelist(const struct swarmkv_reply *reply, struct swarmkv_node_addr **node_list, size_t *n_node);

int swarmkv_node_addr_list_exists(const struct swarmkv_node_addr *list, size_t n_list, const struct swarmkv_node_addr *addr);
int swarmkv_node_addr_list_remove(struct swarmkv_node_addr *list, size_t n_list, const struct swarmkv_node_addr *addr);

void node_addr2reply(const struct swarmkv_node_addr *node, struct swarmkv_reply* reply[2]);
void sds2node_addr(const sds args[2], struct swarmkv_node_addr *node);
void node_addr2sds(const struct swarmkv_node_addr *node, sds argv[2]);

int swarmkv_node_compare(const struct swarmkv_node *node1, const struct swarmkv_node *node2);
void swarmkv_node_copy(struct swarmkv_node *dst, const struct swarmkv_node *src);
int swarmkv_node_addr_compare(const struct swarmkv_node_addr *addr1, const struct swarmkv_node_addr *addr2);
void swarmkv_node_addr_copy(struct swarmkv_node_addr *dst, const struct swarmkv_node_addr *src);

int http_blocking_request(enum evhttp_cmd_type type, const char* host, unsigned int port, const char* url, sds req_body, sds *resp_body);

typedef void exec_cmd_func(struct swarmkv *handle, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer, struct future *f);



