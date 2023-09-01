#pragma once

#include "swarmkv.h"
#include "swarmkv_common.h"
void exec_for_local(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *target_node, swarmkv_on_reply_callback_t * cb, void * cb_arg);
struct event_base *swarmkv_get_event_base(struct swarmkv *db);