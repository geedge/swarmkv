#pragma once
#include "swarmkv_message.h"
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
struct swarmkv_net;

struct swarmkv_net *swarmkv_net_new(struct event_base *evbases[], int nr_worker_threads, struct swarmkv_options *opts, struct log_handle *logger, char **err);
void swarmkv_net_free(struct swarmkv_net *net);
void swarmkv_net_set_on_msg_callback(struct swarmkv_net *net, on_msg_callback_t *cb, void *cb_arg);
int swarmkv_net_send(struct swarmkv_net *net, const node_t *dest, struct swarmkv_msg *msg);

void swarmkv_net_set_monitor_handle(struct swarmkv_net *net, struct swarmkv_module *mod_monitor);

void swarmkv_net_stop(struct swarmkv_net *net);
int swarmkv_net_send(struct swarmkv_net *net, const node_t *dest, struct swarmkv_msg *msg);

struct snet_info
{
	long long connections;
	long long pending_rpcs;
	long long timed_out_rpcs;

	long long input_bytes;
	long long input_cmds;
	long long input_replies;
	long long input_msgs;	//input_cmds + input_replies
	long long unknown_sequence;

	long long output_bytes;
	long long output_cmds;
	long long output_replies;
	long long output_msgs;	//output_cmds + output_replies
	
	long long input_buffer_sz;
	long long output_buffer_sz;
	
	double instantaneous_input_kbps;
	double instantaneous_output_kbps;

	double instantaneous_input_cps;
	double instantaneous_output_cps;
};
void swarmkv_net_info(struct swarmkv_net *net, struct snet_info *info);