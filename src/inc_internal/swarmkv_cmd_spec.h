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


