#pragma once

#define	error_wrong_type			"WRONGTYPE Operation against a key holding the wrong kind of value"
#define	error_wrong_number_of_arg	"ERR wrong number of arguments for `%s` command"
#define	error_unknown_command		"ERR unknown command `%s`, with args beginning with: `%s`,"
#define	error_value_not_integer		"ERR value is not an integer or out of range"
#define error_parameter_not_valid_integer "ERR parameter `%s` is not an integer or out of range"
#define error_parameter_not_valid_string "ERR parameter `%s` is not valid"

#define	error_cluster_leader_not_found	"ERR cluster leader not found"
#define error_cluster_no_node           "ERR cluster has no active node"
#define	error_list_health_node_failed	"ERR list health node failed"
#define	error_read_slot_table_failed	"ERR read slot table failed"
#define error_keyspace_obj_owner_not_found "ERR can't find object owner from keyspace"

#define error_cluster_addnode_node_is_not_active	"ERR candinate node is not active"
#define error_cluster_addnode_node_has_slot 		"ERR candinate node already has some slots"

