#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "future_promise.h"

struct swarmkv_rpc_mgr;
struct swarmkv_rpc;
struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new(struct event_base *evbases[], int nr_threads, long long default_timeout_us);
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr);
//Return a sequence number, which can be used to complete the request
struct swarmkv_rpc *swarmkv_rpc_launch(struct swarmkv_rpc_mgr *mgr, int thread_id, struct future *f);
long long swarmkv_rpc_get_sequence(const struct swarmkv_rpc *rpc);
void swarmkv_rpc_set_peer(struct swarmkv_rpc *rpc, const node_t *peer);
void swarmkv_rpc_set_timeout(struct swarmkv_rpc *rpc, long long timeout_us);
//Return latency in microseconds, or -1 if failed to complete the rpc.
long long swarmkv_rpc_complete(struct swarmkv_rpc_mgr *mgr, int thread_id, long long sequence, void *response);
long long swarmkv_rpc_mgr_count(struct swarmkv_rpc_mgr *mgr, int thread_id);
struct swarmkv_rpc_mgr_info
{
    long long timed_out_rpcs;
    long long unknown_sequence;
};
void swarmkv_rpc_mgr_info(struct swarmkv_rpc_mgr *mgr, struct swarmkv_rpc_mgr_info *info);