#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result get_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	const char *string_val=NULL;
	size_t string_sz=0;
	if(!obj)
	{
		if(accessing_node)
		{
			*reply=swarmkv_reply_new_nil();
			return FINISHED;
		}
		else
		{
			return NEED_KEY_ROUTE;
		}
	}
	
	if(obj->type==OBJ_TYPE_STRING)
	{
		lww_register_get0(obj->reg, &string_val, &string_sz);
		*reply=swarmkv_reply_new_string(string_val, string_sz);
	}
	else if(obj->type==OBJ_TYPE_COUNTER)
	{
		long long counter=PN_counter_get(obj->counter);
		*reply=swarmkv_reply_new_string_fmt("%lld", counter);
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result set_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	const sds value=cmd->argv[2];
	
	char *endptr=NULL;


	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		if(accessing_node)
		{
			obj=swarmkv_store_add_key(mod_store, key, NULL, 0);
		}
		else
		{
			return NEED_KEY_ROUTE;
		}
	}
	long long int_value=0;
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		uuid_t uuid;
		assert(obj->raw==NULL);
		store_get_uuid(mod_store, uuid);
		int_value=strtol(cmd->argv[2], &endptr, 10);
		if(*endptr!='\0')
		{
			obj->type=OBJ_TYPE_STRING;			
			obj->reg=lww_register_new((const char*)uuid, sizeof(uuid));
		}
		else
		{
			obj->type=OBJ_TYPE_COUNTER;
			obj->counter=PN_counter_new((const char*)uuid, sizeof(uuid));
		}
	}
	if(obj->type==OBJ_TYPE_STRING)
	{
		lww_register_set(obj->reg, value, sdslen(value));
		*reply=swarmkv_reply_new_status("OK");
		sobj_need_sync(mod_store, obj);
	}
	else if(obj->type==OBJ_TYPE_COUNTER)
	{
		PN_counter_set(obj->counter, int_value);		
		*reply=swarmkv_reply_new_status("OK");
		sobj_need_sync(mod_store, obj);
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;
}
enum cmd_exec_result incrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* INCRBY key increment*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	long long increment=0, value=0;
	char *endptr=NULL;
	uuid_t uuid;
	increment=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0')
	{
		*reply=swarmkv_reply_new_error(error_value_not_integer);
		return FINISHED; 		
	}
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		if(accessing_node)
		{
			obj=swarmkv_store_add_key(mod_store, key, NULL, 0);
		}
		else
		{
			return NEED_KEY_ROUTE;
		}
	}
	if(obj->type==OBJ_TYPE_UNDEFINED||obj->type==OBJ_TYPE_COUNTER)
	{
		if(!obj->counter)
		{
			store_get_uuid(mod_store, uuid);
			obj->counter=PN_counter_new((const char*)uuid, sizeof(uuid));
			obj->type=OBJ_TYPE_COUNTER;
		}
		value=PN_counter_incrby(obj->counter, increment);
		*reply=swarmkv_reply_new_integer(value);		
		sobj_need_sync(mod_store, obj);
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;
}

