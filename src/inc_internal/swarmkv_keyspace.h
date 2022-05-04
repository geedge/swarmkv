#pragma once
#include "swarmkv_common.h"
#include "swarmkv/swarmkv.h"

#include <unistd.h>
#include <string.h>



struct swarmkv_module* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, void *logger, char **err);
void swarmkv_keyspace_free(struct swarmkv_module* mod_keyspace);
void swarmkv_keyspace_set_send_cmd_func(struct swarmkv_module *mod_keyspace, send_cmd_func *send_cmd, void *handle_send_cmd);

void keyspace_get_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_get_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_set_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd,  const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_set_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_setnxget_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_setnxget_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_del_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_del_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_setslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_setslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_getkeysinslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_getkeysinslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_addkeystoslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_addkeystoslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void keyspace_delslotkeys_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_delslotkeys_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);


void keyspace_keys_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result keyspace_keys_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void expire_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result expire_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

void ttl_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);
enum cmd_fastpath_result ttl_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);


enum key_route_result
{
	__INVALID,
	NOT_FOUND,
	FOUND,
	ASK
};	
enum key_route_result swarmkv_keyspace_reply2node(const struct swarmkv_reply *reply, struct swarmkv_node_addr *out);


