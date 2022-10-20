#pragma once
#include "swarmkv_common.h"
struct swarmkv_net;

typedef void timeout_cb(void *arg);

typedef void on_cmd_callback_t(const struct swarmkv_node_addr *peer, unsigned long long sequence, const void *data, size_t size, void *arg);
typedef void on_reply_callback_t(const struct swarmkv_node_addr *peer, const void *data, size_t size, void *arg);
void swarmkv_net_send_cmd(struct swarmkv_net* net, const struct swarmkv_node_addr* peer, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg);
void swarmkv_net_send_reply(struct swarmkv_net* net, const struct swarmkv_node_addr* peer, const void *data, size_t size, unsigned long long sequence);

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, struct swarmkv_node_addr* self, void* logger, char **err);
void swarmkv_net_free(struct swarmkv_net* net);
void swarmkv_net_set_on_cmd_cb(struct swarmkv_net* net, on_cmd_callback_t on_cmd_cb, void* arg);
void swarmkv_net_timer_add(struct swarmkv_net *net, int thread_id, int seconds, int is_persist, timeout_cb* cb, void *cb_arg);


void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id);
void swarmkv_net_stop(struct swarmkv_net* net);

