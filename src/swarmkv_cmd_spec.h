#pragma once

#include "swarmkv_common.h"
#include "uthash.h"

struct swarmkv_module;

enum cmd_group
{
	GROUP_KEYSPACE,
	GROUP_STRING,
	GROUP_HASH,
	GROUP_TOKEN_BUCKET,
	GROUP_CRDT,
	GROUP_CLUSTER
};
	
/* The following refer what the command actually does with the value or metadata
 * of the key, and not necessarily the user data or how it affects it.
 * Each key-spec may must have exactly one of these. Any operation that's not
 * distinctly deletion, overwrite or read-only would be marked as RW. */
enum cmd_key_flag
{
	CMD_KEY_NA,	/*Not Applicable - Irrelavant to any key operation*/
		
	CMD_KEY_RO,	/* Read-Only - Reads the value of the key. */
	CMD_KEY_RW,	/* Read-Write - Modifies the data stored in the
                 * value of the key or its metadata. */
	CMD_KEY_OW,	/* Overwrite - Overwrites the the key or its metadata, 
				create the key if not-exisit. */
	CMD_KEY_RM	/* Deletes the key. */
};
typedef int swarmkv_module_gettid_func_t(struct swarmkv_module *mod, sds key);
struct swarmkv_module
{
	char name[SWARMKV_SYMBOL_MAX];
	void *mod_ctx;
};

enum key_not_found_reply
{
	REPLY_NA,	/*Not Applicable*/
	REPLY_INT_0,
	REPLY_INT_MINORS1,
	REPLY_NIL,
	REPLY_EMPTY_ARRAY,
	REPLY_STR_NONE,
	REPLY_ERROR
};

typedef enum cmd_exec_result command_proc_func(struct swarmkv_module *module, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

#define	KEY_OFFSET_NONE		-1
#define KEY_OFFSET_TID 		-2
#define KEY_OFFSET_SLOTID	-3
struct swarmkv_cmd_spec
{
	const char *name;
	const char *hint;
	int arity; /* Number of arguments, it is possible to use -N to say >= N */
    int key_offset; /* The argument that's a key (-1 = no keys) */
	enum cmd_key_flag flag; /* Command flag, see CMD_*. */
	enum key_not_found_reply nokey_reply; 
	int auto_route; /* The node that executes the command must be explicitly specifeid. */

	command_proc_func *proc;
	struct swarmkv_module *module;
	UT_hash_handle hh;
};
int command_spec_is_sufficient_args(const struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd);
struct swarmkv_module *swarmkv_command_table_new();
void swarmkv_command_table_free(struct swarmkv_module *table);
void swarmkv_command_table_register(struct swarmkv_module *table, const char *name, const char *hint,
						int arity, int key_offset, enum cmd_key_flag flag, enum key_not_found_reply failover, int auto_route,
						command_proc_func *proc, struct swarmkv_module *module);
const struct swarmkv_cmd_spec *swarmkv_command_table_get_spec_by_argv(const struct swarmkv_module *table, size_t argc, char* const argv[]);

size_t swarmkv_command_table_find_possible_names(const struct swarmkv_module *mod_table, const char *prefix, const char* cmd_names[], size_t sz);
char *swarmkv_command_table_get_command_hint(const struct swarmkv_module *mod_table, const char *cmd_name);
size_t swarmkv_command_table_count(const struct swarmkv_module *mod_table);
size_t swarmkv_command_table_list_names(const struct swarmkv_module *mod_table, const char* cmd_names[], size_t sz);
size_t swarmkv_command_table_count(const struct swarmkv_module *mod_table);
enum cmd_exec_result command_list_command(struct swarmkv_module *mod_command_table, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);