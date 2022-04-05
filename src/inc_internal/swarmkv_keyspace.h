#pragma once
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv/swarmkv.h"
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>


#include <unistd.h>
#include <string.h>



struct swarmkv_module* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, void *logger, char **err);
void swarmkv_keyspace_free(struct swarmkv_module* mod_keyspace);

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


enum cmd_fastpath_result cluster_nodes_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_fastpath_result cluster_slots_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);



enum key_route_result
{
	__INVALID,
	NOT_FOUND,
	FOUND,
	ASK
};	
enum key_route_result swarmkv_keyspace_reply2node(const struct swarmkv_reply *reply, struct swarmkv_node_addr *out);


