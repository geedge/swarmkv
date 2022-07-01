#pragma once
#include "swarmkv_common.h"
#include "swarmkv/swarmkv.h"

#include <unistd.h>
#include <string.h>



struct swarmkv_module* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, void *logger, char **err);
void swarmkv_keyspace_free(struct swarmkv_module* mod_keyspace);

void swarmkv_keyspace_set_exec_cmd_func(struct swarmkv_module *mod_keyspace, exec_cmd_func *func, void *func_handle);


enum cmd_exec_result keyspace_rlist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_radd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_xradd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result del_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);

enum cmd_exec_result keyspace_setslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_getkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_addkeystoslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_delslotkeys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);


enum cmd_exec_result keyspace_keys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);

enum cmd_exec_result expire_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result ttl_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result persist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);


enum key_route_result
{
	__INVALID,
	NOT_FOUND,
	FOUND,
	ASK
};	
enum key_route_result swarmkv_keyspace_reply2node(const struct swarmkv_reply *reply, struct swarmkv_node_addr *out);


