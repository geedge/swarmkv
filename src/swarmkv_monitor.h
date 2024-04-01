#pragma once
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv/swarmkv.h"

#include <unistd.h>
#include <string.h>

struct swarmkv_module *swarmkv_monitor_new(const struct swarmkv_options *opts, struct swarmkv *db);
void swarmkv_monitor_register_command(struct swarmkv_module *mod_monitor, const char *command);
void swarmkv_monitor_free(struct swarmkv_module *mod_monitor);
void swarmkv_monitor_record_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd, long long latency_usec, enum cmd_exec_result result);
void swarmkv_monitor_register_event(struct swarmkv_module *mod_monitor, const char *event);
void swarmkv_monitor_record_peer(struct swarmkv_module *mod_monitor, node_t *peer, long long latency_usec, int thread_id);
void swarmkv_monitor_record_event(struct swarmkv_module *mod_monitor, const char *event_name, long long latency_usec);

enum cmd_exec_result latency_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result monreg_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);