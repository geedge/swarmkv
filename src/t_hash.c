#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "or_map.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result hset_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
//HSET key field value [field value ...]
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
		obj->hash=OR_map_new(uuid);
		obj->type=OBJ_TYPE_HASH;
	}
	size_t n_to_add=(cmd->argc-2)/2;
	size_t i=0;
	int ret=0, n_added=0;
	long long int_value=-1;	
   
	sds field;
    sds value;
		
	if(obj->type==OBJ_TYPE_HASH)
	{
		for(i=0; i<n_to_add; i++)
		{
            field=cmd->argv[2+i*2];
            value=cmd->argv[2+i*2+1];

            ret=OR_map_set(obj->hash, field, sdslen(field),
                            value, sdslen(value));
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
enum cmd_exec_result hmget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
    //HMGET key field [field ...]
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
    if(obj->type!=OBJ_TYPE_HASH)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	size_t n_to_get=cmd->argc-2;
    struct OR_map_kv *kv_got=NULL;
    *reply=swarmkv_reply_new_array(n_to_get);
    for(size_t i=0; i<n_to_get; i++)
    {
        kv_got=OR_map_get(obj->hash, cmd->argv[i+2], sdslen(cmd->argv[i+2]));
        if(!kv_got)
        {
            (*reply)->elements[i]=swarmkv_reply_new_nil();
        }
        else
        {
            switch(kv_got->type)
            {
                case TYPE_STRING:
                    (*reply)->elements[i]=swarmkv_reply_new_string(kv_got->string, kv_got->str_len);
                    break;
                case TYPE_INTEGER:
                    (*reply)->elements[i]=swarmkv_reply_new_string_from_integer(kv_got->integer);
                    break;
                default:
                    assert(0);
                    break;
            }
            OR_map_kv_free(kv_got);
        }
    }
	return FINISHED;    
}
enum cmd_exec_result hget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
    enum cmd_exec_result ret;
    struct swarmkv_reply *tmp_reply=NULL;
    ret=hmget_command(mod_store, cmd, accessing_node, &tmp_reply);
    if(ret==FINISHED)
    {
        if(tmp_reply->type==SWARMKV_REPLY_ARRAY)
        {
            *reply=swarmkv_reply_dup(tmp_reply->elements[0]);
            swarmkv_reply_free(tmp_reply);
        }
        else
        {
            *reply=tmp_reply;
        }
    }
    return ret;
}