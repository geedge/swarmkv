#pragma once
#include "swarmkv/swarmkv.h"

struct swarmkv_rr_mgr;
struct swarmkv_rr_mgr *swarmkv_rr_mgr_new();
void swarmkv_rr_mgr_free(struct swarmkv_rr_mgr *mgr);
typedef void swarmkv_rr_callback_func_t(const struct swarmkv_reply* reply, void * arg);

unsigned long long swarmkv_rr_add(struct swarmkv_rr_mgr *mgr, swarmkv_rr_callback_func_t * cb, void * cb_arg);
int swarmkv_rr_get(struct swarmkv_rr_mgr *mgr, unsigned long long rr_sequence, swarmkv_rr_callback_func_t ** cb, void ** cb_arg);


