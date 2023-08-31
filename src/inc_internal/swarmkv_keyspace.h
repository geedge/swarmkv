#pragma once
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv/swarmkv.h"

#include <unistd.h>
#include <string.h>



struct swarmkv_module *swarmkv_keyspace_new(struct event_base *evbases[], int nr_threads, struct swarmkv_options *opts, const char *db_name, struct log_handle *logger, char **err);
void swarmkv_keyspace_start(struct swarmkv_module *mod_keyspace);
void swarmkv_keyspace_free(struct swarmkv_module* mod_keyspace);
void swarmkv_keyspace_periodic(struct swarmkv_module *mod_keyspace, int thread_id);
int swarmkv_keyspace_slot2tid(struct swarmkv_module *mod_keyspace, int slot_id);
struct keyspace_info
{
	unsigned int health_check_port;
	long long slots;
	long long keys;
	long long expires;	
};
void swarmkv_keyspace_info(struct swarmkv_module *mod_keyspace, struct keyspace_info *info);
void swarmkv_keyspace_set_exec_cmd_handle(struct swarmkv_module *mod_keyspace, struct swarmkv *db);
void swarmkv_keyspace_set_monitor_handle(struct swarmkv_module *mod_keyspace, struct swarmkv_module *mod_monitor);

enum cmd_exec_result keyslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_rlist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_radd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_xradd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result del_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_rdel_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_exists_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

enum cmd_exec_result keyspace_setslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_getkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_addkeystoslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_delslotkeys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result keyspace_countkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

enum cmd_exec_result keyspace_keys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

enum cmd_exec_result expire_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result ttl_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
enum cmd_exec_result persist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);


enum key_route_result
{
	__INVALID,
	NOT_FOUND,
	FOUND,
	ASK
};	