#pragma once

#define	error_wrong_type			"WRONGTYPE Operation against a key holding the wrong kind of value"
#define	error_wrong_number_of_arg	"ERR wrong number of arguments for `%s` command"
#define error_no_target_node       "ERR executing `%s` requires a target node"
#define	error_unknown_command		"ERR unknown command `%s`, with args beginning with: `%s`,"
#define	error_value_not_integer		"ERR value is not an integer or out of range"
#define error_arg_not_valid_integer "ERR arg `%s` is not an integer or out of range"
#define error_arg_not_valid_address "ERR arg `%s` is not `IP:port` format"
#define error_arg_not_valid_float "ERR arg `%s` is not a valid float or out of range"
#define error_arg_not_valid_uuid "ERR arg `%s` is not a valid UUID"
#define error_arg_parse_failed    "ERR arg `%s` parse failed"
#define error_arg_string_should_be    "ERR arg `%s` should be `%s`"
#define error_need_additional_arg   "ERR arg `%s` should be fllowed by more args"
#define erorr_subcommand_syntax     "ERR unknown subcommand or wrong number of arguments for '%.128s'. Try %s HELP."

#define error_too_many_redirects     "ERR too many redirects, the lastest is `%s`"
#define	error_cluster_leader_not_found	"ERR cluster leader not found"
#define error_cluster_no_node           "ERR cluster has no active node"
#define	error_list_health_node_failed	"ERR list health node failed"
#define	error_read_slot_table_failed	"ERR read slot table failed"
#define error_keyspace_obj_owner_not_found "ERR can't find object owner from keyspace"

#define error_cluster_addslotowner_node_is_not_active	"ERR candinate node %s is not active"
#define error_cluster_addslotowner_node_has_slot 		"ERR candinate node %s already has some slots"

#define error_network_error "ERR network error of peer %s, reason: %s"
#define error_thread_rpc_buffer_full "ERR thread rpc buffer is full"