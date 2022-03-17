#pragma once
#include "swarmkv_common.h"
#include "swarmkv/swarmkv.h"
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>


#include <unistd.h>
#include <string.h>

struct swarmkv_keyspace;


struct swarmkv_keyspace* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, struct event_base * evbase, void *logger, char **err);
void swarmkv_keyspace_free(struct swarmkv_keyspace* keyspace);



typedef int (
*keyspace_send_msg_func
)   (
    struct swarmkv_keyspace* keyspace,
    struct swarmkv_node* node,
    void *user_data,
    const char* send_buff,
    size_t buff_sz
    );
//send_msg_cb is used to sent raft messages and gossip ping messages to peers.
int swarmkv_keyspace_set_callback(struct swarmkv_keyspace* keyspace, keyspace_send_msg_func send_msg_cb);
int swarmkv_keyspace_recevie_msg(struct swarmkv_keyspace* keyspace, int node_id, const char* recv_buf, size_t buff_sz);
int swarmkv_keyspace_periodic(struct swarmkv_keyspace* keyspace, int msec_since_last_period);



int swarmkv_keyspace_set_consul_port(struct swarmkv_keyspace* ks, unsigned int consul_port);
int swarmkv_keyspace_set_dryrun(struct swarmkv_keyspace* ks);
int swarmkv_keyspace_set_selfip(struct swarmkv_keyspace* ks, const char* ip);
int swarmkv_keyspace_is_myself(struct swarmkv_keyspace* ks, const struct swarmkv_node *node);

int swarmkv_keyspace_get_key_owner(struct swarmkv_keyspace* ks, sds key, struct swarmkv_node *owner);
struct swarmkv_cmd *swarmkv_keyspace_cmd_new(enum swarmkv_operator store_op, const sds key, const struct swarmkv_node *in);
enum key_route_result
{
	__INVALID,
	NOT_FOUND,
	FOUND,
	ASK
};	
enum key_route_result swarmkv_keyspace_reply2node(enum swarmkv_operator keyspace_op, const struct swarmkv_reply *reply, struct swarmkv_node *out);
void swarmkv_keyspace_exec(struct swarmkv_keyspace* ks, const struct swarmkv_cmd *cmd, struct swarmkv_reply **out);


