#pragma once
#include "swarmkv_common.h"
struct swarmkv_net;

typedef void timeout_cb(void *arg);

typedef void on_cmd_callback_t(const node_t *peer, unsigned long long sequence, const void *data, size_t size, void *arg);
typedef void on_reply_callback_t(const node_t *peer, const void *data, size_t size, void *arg);
void swarmkv_net_send_cmd(struct swarmkv_net *net, const node_t *peer, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg);
void swarmkv_net_send_reply(struct swarmkv_net *net, const node_t *peer, const void *data, size_t size, unsigned long long sequence);

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, node_t *self, void *logger, char **err);
void swarmkv_net_free(struct swarmkv_net *net);
void swarmkv_net_set_on_cmd_cb(struct swarmkv_net *net, on_cmd_callback_t on_cmd_cb, void* arg);
void swarmkv_net_timer_add(struct swarmkv_net *net, int thread_id, int usec, int is_persist, timeout_cb *cb, void *cb_arg);
void swarmkv_net_set_monitor_handle(struct swarmkv_net* net, struct swarmkv_module *mod_monitor);

void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id);
void swarmkv_net_stop(struct swarmkv_net* net);

struct snet_info
{
	long long connections;
	long long pending_rpcs;
	long long timed_out_rpcs;

	long long input_bytes;
	long long input_cmds;
	long long input_replies;
	long long input_msgs;	//input_cmds + input_replies

	long long output_bytes;
	long long output_cmds;
	long long output_replies;
	long long output_msgs;	//output_cmds + output_replies
	
	double instantaneous_input_kbps;
	double instantaneous_output_kbps;

	double instantaneous_input_cps;
	double instantaneous_output_cps;
};
void swarmkv_net_info(struct swarmkv_net *net, struct snet_info *info);