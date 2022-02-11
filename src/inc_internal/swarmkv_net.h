#pragma once
#include "swarmkv_common.h"
struct swarmkv_net;
typedef int (*net_recv_data_func)   (
    const char* ip,
    unsigned int port,
    void *arg,
    const char* data,
    size_t data_len
    );
struct swarmkv_net* swarmkv_net_new(unsigned int listen_port, int thread_num, void* logger);
void swarmkv_net_free(struct swarmkv_net* net);
void swarmkv_net_set_recv_cb(struct swarmkv_net* net, net_recv_data_func recv_cb, void* arg);
int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node* dest, const char* data, size_t datalen);
void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id);
struct event_base * swarmkv_net_get_eventbase(struct swarmkv_net* net, int thread_id);



