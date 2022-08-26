#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "utarray.h"

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
		sobj_add_replica_node(obj, accessing_node);
		if(obj->type!=OBJ_TYPE_UNDEFINED)
		{
			sobj_serialize(obj, &blob, &blob_sz);
			*reply=swarmkv_reply_new_string(blob, blob_sz);
			free(blob);
		}
		else
		{
			*reply=swarmkv_reply_new_nil();
		}
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
	uuid_t uuid;
	store_get_uuid(mod_store, uuid);
	if(obj)
	{	
		sobj_merge_blob(obj, blob, sdslen(blob), uuid);
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
enum cmd_exec_result crdt_join_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	/* CRDT JOIN <key>*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	assert(accessing_node!=NULL);//should never invoked by local
	obj=store_lookup(mod_store, key);
	int added=0;
	if(obj)
	{	
		added=sobj_add_replica_node(obj, accessing_node);
		*reply=swarmkv_reply_new_integer(added);
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}
	return FINISHED;
}

//CRDT keys IP Port pattern
enum cmd_exec_result crdt_keys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
    int i=0, n_matched=0;
    UT_array* matched_replies=NULL;
    sds pattern;
    struct swarmkv_node_addr target_node;

    if(cmd->argc < 5)
    {
        *reply=swarmkv_reply_new_nil();
        return FINISHED;
    }

    sds2node_addr(cmd->argv+2, &target_node);
    pattern = cmd->argv[4];

    if(!accessing_node)
    {
        *reply=swarmkv_reply_new_node(&target_node, 1);
        return REDIRECT;
    }

    store_each_obj(mod_store, pattern, (void **)&matched_replies);
	n_matched=utarray_len(matched_replies);
	if(n_matched>0)
	{
		*reply=swarmkv_reply_new_array(n_matched);
		for(i=0; i<n_matched; i++)
		{
			(*reply)->elements[i]=*(struct swarmkv_reply **)utarray_eltptr(matched_replies, i);
		}
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}
	utarray_free(matched_replies);
	matched_replies=NULL;
	return FINISHED;
}
