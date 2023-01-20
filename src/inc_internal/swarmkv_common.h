#pragma once
#include "sds.h"
#include "log.h"
#include "swarmkv_limits.h"
#include "swarmkv/swarmkv.h"
#include "future_promise.h"

#include <netinet/ip.h>
#include <uuid/uuid.h>

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
	FINISHED,
	ASYNC_WAIT
};
	

/*
struct swarmkv_node_addr
{
	char ip_addr[INET6_ADDRSTRLEN];	//#define INET6_ADDRSTRLEN 46 (#include <arpa/inet.h>, see man inet_ntop
	unsigned int cluster_port;	//host order

};*/
#define MAX_IPV4_ADDR_LEN	15
typedef struct{
	char addr[MAX_IPV4_ADDR_LEN+5+1]; //IP:port
}node_t;

enum slot_state
{
	STATE_STABLE=0,
	STATE_MIGRATING,
	STATE_IMPORTING
};

struct key_slot
{
	int slot_id;
	node_t owner;
	uuid_t uuid;
};
void json2keyslots(sds json_buffer, void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container);
sds keyslots2json(void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container);
void health_response2active_nodes(const char *resp_buff, node_t *nodes, uuid_t *uuids, size_t *n_node);
void leader_response2leader_node(const char *resp_body, node_t *leader);
int key_hash_slot(const char* key, size_t keylen);
int str2integer(const char *str, long long *integer);
char *str_replace(char *orig, char *rep, char *with);
struct swarmkv_options
{
	unsigned int health_check_port;
	unsigned int cluster_port;
	int dryrun;
	int run_for_leader_flag;
	unsigned int consul_port;
    unsigned int cluster_timeout_us;
	unsigned int sync_interval_us;
	struct log_handle *logger;
	int loglevel;
	char bind_address[MAX_IPV4_ADDR_LEN];
	char consul_agent_host[MAX_IPV4_ADDR_LEN];
	uuid_t bin_uuid;
	size_t nr_worker_threads;
	int is_assigned_to_db;
};

struct swarmkv_cmd
{
	size_t argc;
	sds *argv;
};

struct swarmkv_cmd *swarmkv_cmd_new(size_t argc);
void swarmkv_cmd_free(struct swarmkv_cmd *p);
struct swarmkv_cmd *swarmkv_cmd_dup(const struct swarmkv_cmd *origin);

struct swarmkv_reply *swarmkv_reply_new_string(const char *str, size_t sz);
struct swarmkv_reply *swarmkv_reply_new_string_fmt(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_new_string_from_integer(long long integer);
struct swarmkv_reply *swarmkv_reply_new_verbatim(const char *str, size_t sz, const char *ext);

struct swarmkv_reply *swarmkv_reply_new_integer(long long integer);
struct swarmkv_reply *swarmkv_reply_new_array(size_t n_element);
struct swarmkv_reply *swarmkv_reply_new_node(node_t *node, int is_ask);
struct swarmkv_reply *swarmkv_reply_new_nil(void);
struct swarmkv_reply *swarmkv_reply_new_status(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_new_error(const char *format, ...);
struct swarmkv_reply *swarmkv_reply_dup(const struct swarmkv_reply *origin);
void swarmkv_reply_merge_array(struct swarmkv_reply **dst, struct swarmkv_reply *src);
void swarmkv_reply_append_string(struct swarmkv_reply **dst, struct swarmkv_reply *src);

int node_list_new_from_reply(node_t **node_list, size_t *n_node, const struct swarmkv_reply *reply);

int node_list_exists(const node_t *list, size_t n_list, const node_t *node);
int node_list_remove(node_t *list, size_t n_list, const node_t *node);

sds node_print_json(const node_t *self, uuid_t uuid);
sds node_addr2sds(const node_t *node);
int node_init_from_sds(node_t *node, const char *addr_str);
int node_init_from_reply(node_t *node, const struct swarmkv_reply *reply);
int node_compare(const node_t *node1, const node_t *node2);
void node_copy(node_t *dst, const node_t *src);
void node_init(node_t *node, const char *ipv4, unsigned int port);
void node_init_from_string(node_t *node, const char *addr_str);
int node_parse(const node_t *node, unsigned int *port, char *addr_str, size_t sz_addr);
void node_init_from_sockaddr(node_t *node, const struct sockaddr * sa);
void node_to_sockaddr(const node_t *node, struct sockaddr *sockaddr);
int http_blocking_request(enum evhttp_cmd_type type, const char* host, unsigned int port, const char* url, sds req_body, sds *resp_body);

typedef void exec_cmd_func(struct swarmkv *handle, const struct swarmkv_cmd *cmd, const node_t *accessing_node, const node_t *target_node, struct future *f);
struct swarmkv_reply *swarmkv_command_on_argv(struct swarmkv *db, const char *target, int argc, sds *argv);