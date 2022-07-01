#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result crdt_pull_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT PULL key */
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	char *blob=NULL;
	size_t blob_sz=0;
	assert(accessing_node!=NULL);//should never invoked by local
	obj=store_lookup(mod_store, key);
	if(obj)
	{	
		sobj_serialize(obj, &blob, &blob_sz);
		*reply=swarmkv_reply_new_string(blob, blob_sz);
		free(blob);
		sobj_add_replica_node(obj, accessing_node);
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}
	return FINISHED;
	
}
enum cmd_exec_result crdt_push_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT PUSH key blob*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	const sds blob=cmd->argv[3];	
	assert(accessing_node!=NULL);//should never invoked by local
	obj=store_lookup(mod_store, key);
	if(obj)
	{	
		sobj_merge_blob(mod_store, obj, blob, sdslen(blob));
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}	
	return FINISHED;
}

enum cmd_exec_result crdt_del_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT DEL <key> */

	sds key=cmd->argv[2];
	struct sobj *obj=NULL;
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		store_remove_obj(mod_store, obj);
		sobj_free(obj);	
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}	
	return FINISHED;
}

