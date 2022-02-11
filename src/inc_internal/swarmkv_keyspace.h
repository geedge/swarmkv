#pragma once
#include "swarmkv_common.h"

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>


#include <unistd.h>
#include <string.h>

struct swarmkv_keyspace;


struct swarmkv_keyspace* swarmkv_keyspace_new(const char* cluster_name, struct event_base* evbase, unsigned int http_port, unsigned int p2p_port, void *logger);
int swarmkv_keyspace_init(struct swarmkv_keyspace* ks);
void swarmkv_keyspace_free(struct swarmkv_keyspace* keyspace);
int swarmkv_keyspace_get_keyowner(struct swarmkv_keyspace* keyspace, const char* key, size_t keylen, const struct swarmkv_node* out);
//int swarmkv_keyspace_handle_failed_node(struct swarmkv_keyspace* keyspace, int failed_node_id);

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

