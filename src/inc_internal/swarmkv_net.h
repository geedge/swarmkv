#pragma once
#include "swarmkv_common.h"
struct swarmkv_net;
typedef void net_recv_data_func (
	const struct swarmkv_node_addr * peer,
    const char* data,
    size_t data_len,    
    void *arg
    );
typedef void timeout_cb(void *arg);

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, const struct swarmkv_node_addr* self, void* logger, char **err);
void swarmkv_net_free(struct swarmkv_net* net);
void swarmkv_net_set_recv_cb(struct swarmkv_net* net, net_recv_data_func recv_cb, void* arg);
void swarmkv_net_timer_add(struct swarmkv_net *net, int thread_id, int seconds, int is_persist, timeout_cb* cb, void *cb_arg);

int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node_addr* dest, const sds data);
void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id);
void swarmkv_net_stop(struct swarmkv_net* net);

