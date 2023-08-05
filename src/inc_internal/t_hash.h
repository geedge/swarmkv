#pragma once
#include "swarmkv_common.h"

enum cmd_exec_result hset_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hmget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hdel_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hgetall_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hlen_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hkeys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result hincrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
