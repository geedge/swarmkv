#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result sadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		uuid_t uuid;
		assert(obj->raw==NULL);
		store_get_uuid(mod_store, uuid);
		obj->set=OR_set_new(uuid);
		obj->type=OBJ_TYPE_SET;
	}
	size_t n_to_add=cmd->argc-2;
	size_t i=0;
	int ret=0;
	int n_added=0;
	
	if(obj->type==OBJ_TYPE_SET)
	{
		for(i=0; i<n_to_add; i++)
		{
			ret=OR_set_add(obj->set, cmd->argv[2+i], sdslen(cmd->argv[2+i]));	
			if(ret>0) n_added++;
		}
		*reply=swarmkv_reply_new_integer(n_added);
		sobj_need_sync(mod_store, obj);
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;

}
enum cmd_exec_result srem_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if(obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_SET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	size_t n_to_rm=cmd->argc-2;
	size_t i=0;
	int ret=0;
	int n_removed=0;

	for(i=0; i<n_to_rm; i++)
	{
		ret=OR_set_remove(obj->set, cmd->argv[2+i], sdslen(cmd->argv[2+i]));	
		if(ret>0) n_removed++;
	}
	*reply=swarmkv_reply_new_integer(n_removed);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
enum cmd_exec_result smembers_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if(obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_SET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	struct OR_set_member_list *mlist=NULL;
	mlist=OR_set_members(obj->set);
	if(!mlist)
	{		
		*reply=swarmkv_reply_new_array(0);//return empty array		
		return FINISHED;
	}
	size_t i=0;
	*reply=swarmkv_reply_new_array(mlist->n_member);
	for(i=0; i<mlist->n_member; i++)
	{
		(*reply)->elements[i]=swarmkv_reply_new_string(mlist->members[i], mlist->member_len[i]);
	}
	OR_set_member_list_free(mlist);
	return FINISHED;
}
enum cmd_exec_result sismember_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if(obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_SET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	int ret=0;
	ret=OR_set_is_member(obj->set, cmd->argv[2], sdslen(cmd->argv[2]));
	*reply=swarmkv_reply_new_integer(ret);
	return FINISHED;
}
enum cmd_exec_result scard_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if(obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_SET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	int ret=0;
	ret=OR_set_cardinality(obj->set);
	*reply=swarmkv_reply_new_integer(ret);
	return FINISHED;
}


