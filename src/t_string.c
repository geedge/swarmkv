#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <stdlib.h>

void get_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		if(obj->type!=OBJ_TYPE_STRING)
		{
			*reply=swarmkv_reply_new_error(error_wrong_type);
		}
		else
		{
			*reply=swarmkv_reply_new_string(obj->val_string, sdslen(obj->val_string));
		}
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}

	if(accessing_node && obj)
		store_obj_setnx_accessed_node(mod_store, obj, accessing_node);
	
	return;
}

enum cmd_fastpath_result get_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		get_command_as_owner(mod_store, cmd, NULL, reply);
		return FINISHED;
	}
	obj=store_cache_lookup(mod_store, key);
	if(obj)
	{
		*reply=swarmkv_reply_new_string(obj->val_string, sdslen(obj->val_string));
		return FINISHED;
	}
	return NEED_KEY_ROUTE;
}


void set_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;
	const sds key=cmd->argv[1];
	const sds value=cmd->argv[2];

	obj=store_lookup(mod_store, key);

	if(obj && obj->type!=OBJ_TYPE_STRING)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return;
	}
	if(obj)
	{
		sdsfree(obj->val_string);
		obj->val_string=sdsdup(value);
	}
	else
	{
		obj=swarmkv_obj_new(OBJ_TYPE_STRING, key);
		obj->val_string=sdsdup(value);
		store_add_obj(mod_store, obj);
	}
	*reply=swarmkv_reply_new_status("OK");

	if(accessing_node)
		store_obj_setnx_accessed_node(mod_store, obj, accessing_node);

	store_cache_notify_all(mod_store, CACHE_SET, obj);
	return;
}
enum cmd_fastpath_result set_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	const sds key=cmd->argv[1];
	struct swarmkv_obj *obj=NULL;
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		set_command_as_owner(mod_store, cmd, NULL, reply);
		return FINISHED;
	}
	return NEED_KEY_ROUTE;
}

void del_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	const sds key=cmd->argv[1];
	struct swarmkv_obj *obj=NULL;
	obj=store_lookup(mod_store, key);

	if(obj)
	{
		store_remove_obj(mod_store, obj);
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}

	// I do NOT call store_obj_setnx_accessed_node() here, because
	// - If current node has accessed the key, it is already in the watch list, 
	//   and will be notified by store_cache_notify_all() later.
	// - If not, the key is deleted, cache is unnecessary.
	
	if(obj)
	{
		store_cache_notify_all(mod_store, CACHE_DEL, obj);
		swarmkv_obj_free(obj);
	}
	
	return;
}


enum cmd_fastpath_result del_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	return NEED_KEY_ROUTE;
}
void incrby_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;
	const sds key=cmd->argv[1];

	obj=store_lookup(mod_store, key);
	if(obj && obj->type!=OBJ_TYPE_STRING)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return;
	}
	if(obj)
	{
		long long value=0, delta=0;
		char *endptr=NULL;
		value=strtol(obj->val_string, &endptr, 10);
		if(*endptr!='\0')
		{
			*reply=swarmkv_reply_new_error(error_value_not_integer);
			return;
		}	
		delta=strtol(cmd->argv[2], &endptr, 10);
		if(*endptr!='\0')
		{
			*reply=swarmkv_reply_new_error(error_value_not_integer);
			return;			
		}
		value+=delta;
		sdsfree(obj->val_string);
		obj->val_string=sdsfromlonglong(value);
	}
	else
	{
		obj=swarmkv_obj_new(OBJ_TYPE_STRING, key);
		obj->val_string=sdsdup(cmd->argv[2]);
		store_add_obj(mod_store, obj);
	}
	*reply=swarmkv_reply_new_integer(atoll(obj->val_string));

	if(accessing_node)
		store_obj_setnx_accessed_node(mod_store, obj, accessing_node);

	store_cache_notify_all(mod_store, CACHE_SET, obj);
	return;
}

enum cmd_fastpath_result incrby_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;	
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		incrby_command_as_owner(mod_store, cmd, NULL, reply);
		return FINISHED;
	}
	return NEED_KEY_ROUTE;
}

