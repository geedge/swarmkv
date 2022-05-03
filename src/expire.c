#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <limits.h>
#include <stdlib.h> //strtol

void expire_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	//EXPIRE key seconds
	const sds key=cmd->argv[1];
	char *endptr=NULL;
	long seconds=strtol(cmd->argv[2], &endptr, 10);

	if(*endptr!='\0' || seconds>INT_MAX)
	{
		*reply=swarmkv_reply_new_error(error_value_not_integer);
		return;
	}

	int ret=0;
	ret=store_expire_key(mod_store, key, (int) seconds);
	*reply=swarmkv_reply_new_integer(ret);

	return;
}
enum cmd_fastpath_result expire_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	const sds key=cmd->argv[1];	
	struct swarmkv_obj *obj=NULL;
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		expire_command_as_owner(mod_store, cmd, NULL, reply);
		return FINISHED;
	}
	return NEED_KEY_ROUTE;
}

void ttl_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	//TTL key
	const sds key=cmd->argv[1];
	int ttl=store_ttl_key(mod_store, key);
	if(ttl<0)
	{
		*reply=swarmkv_reply_new_nil();
	}
	else
	{
		*reply=swarmkv_reply_new_integer(ttl);
	}
	
	return;
}

enum cmd_fastpath_result ttl_command_fastpath(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_obj *obj=NULL;
	const sds key=cmd->argv[1];
	
	int ttl=0;
	ttl=store_ttl_key(mod_store, key);
	if(ttl==-2)//key is not exisit
	{
		obj=store_cache_lookup(mod_store, key);
		if(obj)
		{
			ttl=swarmkv_obj_get_ttl(obj);
			*reply=swarmkv_reply_new_integer(ttl);
			return FINISHED;
		}
	}
	return NEED_KEY_ROUTE;

}

