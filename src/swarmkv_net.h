#pragma once
#include "swarmkv_message.h"
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
struct swarmkv_net;

struct swarmkv_net *swarmkv_net_new(struct event_base *evbases[], int nr_threads, struct swarmkv_options *opts, struct log_handle *logger, char **err);
void swarmkv_net_free(struct swarmkv_net *net);

typedef void on_net_msg_callback_t(struct swarmkv_msg *msg, const node_t *peer, int peer_tid, void *arg);
void swarmkv_net_set_on_msg_callback(struct swarmkv_net *net, on_net_msg_callback_t *cb, void *cb_arg);
//swamrkv_net_send takes the ownership of msg.
int swarmkv_net_send(struct swarmkv_net *net, int tid, const node_t *dest, int dest_tid, struct swarmkv_msg *msg, const char **err_str);

void swarmkv_net_set_monitor_handle(struct swarmkv_net *net, struct swarmkv_module *mod_monitor);


struct snet_info
{
	long long output_buf_max;
	long long connections;

	long long input_bytes;
	long long input_msgs;
	long long input_error_msgs;

	long long output_bytes;
	long long output_msgs;
	long long output_drop_msgs;
	
	long long input_buffer_sz;
	long long output_buffer_sz;
	
	double instantaneous_input_kbps;
	double instantaneous_output_kbps;

	double instantaneous_input_msgs;
	double instantaneous_output_msgs;

	double input_compression_ratio;
	double output_compression_ratio;
};
void swarmkv_net_info(struct swarmkv_net *net, struct snet_info *info);