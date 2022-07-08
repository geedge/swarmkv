#pragma once

#include "swarmkv_common.h"
#include "uthash.h"

enum cmd_group
{
	GROUP_KEYSPACE,
	GROUP_STRING,
	GROUP_HASH,
	GROUP_THROTE,
	GROUP_CACHE,
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
typedef enum cmd_exec_result command_proc_func(struct swarmkv_module *module, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply);

struct swarmkv_cmd_spec
{
	const char *name;
	const char *hint;
	int arity; /* Number of arguments, it is possible to use -N to say >= N */
    int key_offset; /* The argument that's a key (-1 = no keys) */
	enum cmd_key_flag flag; /* Command flag, see CMD_*. */	

	command_proc_func *proc;
	struct swarmkv_module *module;
	UT_hash_handle hh;
};


