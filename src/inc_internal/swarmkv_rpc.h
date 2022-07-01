#pragma once
#include "swarmkv/swarmkv.h"

struct swarmkv_rpc_mgr;
struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new();
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr);
typedef void swarmkv_rpc_callback_func_t(const struct swarmkv_reply* reply, void * arg);

unsigned long long swarmkv_rpc_set_callback(struct swarmkv_rpc_mgr *mgr, swarmkv_rpc_callback_func_t * cb, void * cb_arg);
int swarmkv_rpc_get_callback(struct swarmkv_rpc_mgr *mgr, unsigned long long rpc_sequence, swarmkv_rpc_callback_func_t ** cb, void ** cb_arg);


