#pragma once
#include "swarmkv_common.h"
enum cmd_exec_result cmsinitbydim_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsinitbyprob_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsincrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsmquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsrlist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result cmsrclear_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);