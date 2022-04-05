#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

void cache_set_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* CACHE SET <key> <blob> */
	sds key=cmd->argv[2];
	sds blob=cmd->argv[3];
	store_cache_refresh(mod_store, CACHE_SET, key, blob);
	*reply=swarmkv_reply_new_status("OK");
	return;
}
void cache_del_command_as_owner(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* CACHE DEL <key> */

	sds key=cmd->argv[2];
	struct swarmkv_obj *obj=NULL;
	obj=store_cache_lookup(mod_store, key);
	if(obj)
	{
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}	
	store_cache_refresh(mod_store, CACHE_DEL, key, NULL);
	return;
}

