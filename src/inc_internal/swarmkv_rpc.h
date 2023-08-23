#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "future_promise.h"

struct swarmkv_rpc_mgr;
struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new(const struct swarmkv_options *opts, struct event_base *evbases[], int nr_worker_threads);
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr);
//Return a sequence number, which can be used to complete the request
long long swarmkv_rpc_launch(struct swarmkv_rpc_mgr *mgr, int thread_id, struct future *f);
void swarmkv_rpc_complete(struct swarmkv_rpc_mgr *mgr, int thread_id, long long sequence, void *response);
size_t swarmkv_rpc_mgr_count(struct swarmkv_rpc_mgr *mgr, int thread_id);