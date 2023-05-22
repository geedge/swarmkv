#pragma once
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"

// TOPKREV key K [width] [depth] [decay rate]
enum cmd_exec_result topkrev_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
// TOPKADD key member score
enum cmd_exec_result topkadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
// TOPKINFO key, print parameters
enum cmd_exec_result topkinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
// TOPKLIST key   query for all topK
enum cmd_exec_result topklist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
