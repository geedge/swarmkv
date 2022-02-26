#pragma once
#include "swarmkv_common.h"
struct swarmkv_net;
typedef void net_recv_data_func (
	const struct swarmkv_node * peer,
    const char* data,
    size_t data_len,    
    void *arg
    );
struct swarmkv_net* swarmkv_net_new(const struct swarmkv_node* self, int thread_num, void* logger);
void swarmkv_net_free(struct swarmkv_net* net);
void swarmkv_net_set_recv_cb(struct swarmkv_net* net, net_recv_data_func recv_cb, void* arg);
int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node* dest, const sds data);
void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id);
void swarmkv_net_stop(struct swarmkv_net* net);

struct event_base * swarmkv_net_get_eventbase(struct swarmkv_net* net, int thread_id);



