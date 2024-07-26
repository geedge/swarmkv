#pragma once
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"

enum cmd_exec_result ssinitbydim_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result ssinitbycapacity_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result ssadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result sslist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result ssinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result ssquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);