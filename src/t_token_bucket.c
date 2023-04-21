#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "swarmkv_utils.h"
#include "utarray.h"

#include <stdlib.h>
#include <assert.h>
enum cmd_exec_result handle_undefined_object(struct sobj *obj, struct swarmkv_reply **reply)
{
	assert(obj->type==OBJ_TYPE_UNDEFINED);
	node_t replica;
	int ret=0;
	ret=sobj_get_random_replica(obj, &replica);
	if(ret)
	{
		*reply=swarmkv_reply_new_node(&replica, 1);
		return REDIRECT;
	}
	return NEED_KEY_ROUTE;	
}
enum cmd_exec_result tcfg_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TCFG key rate capacity*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	char *endptr=NULL;

	long long capacity=0, rate=0;	
	rate=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0' || capacity<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED; 		
	}

	capacity=strtol(cmd->argv[3], &endptr, 10);
	if(*endptr!='\0' || rate<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
		return FINISHED; 		
	}
	
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;	
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		uuid_t uuid;
		assert(obj->raw==NULL);
		store_get_uuid(mod_store, uuid);
	
		obj->type=OBJ_TYPE_TOKEN_BUCKET;
		obj->bucket=OC_token_bucket_new(uuid, now, capacity, rate);
		
		*reply=swarmkv_reply_new_status("OK");
	}
	else if(obj->type==OBJ_TYPE_TOKEN_BUCKET)
	{
		OC_token_bucket_configure(obj->bucket, now, capacity, rate, 0);
		sobj_need_sync(mod_store, obj);
		
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;
}
enum cmd_exec_result tinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TINFO key*/	
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;		
	}
	//Unlike string, set and hash, TCONSUME and TINFO can only operate on an initialized token bucket.
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct OC_token_bucket_info info;
	memset(&info, 0, sizeof(info));
	struct timeval now;
	gettimeofday(&now, NULL);
	OC_token_bucket_info(obj->bucket, now, &info);
	int i=0;
	*reply=swarmkv_reply_new_array(10);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.CBS);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.CIR);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Consumed");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.consumed);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Refilled");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.refilled);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Available");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.available);
	assert(i==10);
	return FINISHED;
}
enum cmd_exec_result tconsume_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TCONSUME key tokens [NORMAL|FORCE|FLEXIBLE]*/
	
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;		
	}
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	long long request=0, got=0;
	char *endptr=NULL;
	request=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0' || request<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED; 		
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	enum token_bucket_command oc_cmd=TB_CMD_CONSUME_NORMAL;
	if(cmd->argc==4)
	{
		const sds mode=cmd->argv[3];
		if(0==strncasecmp(mode, "NORMAL", sdslen(mode)))
		{
			oc_cmd=TB_CMD_CONSUME_NORMAL;
		}
		if(0==strncasecmp(mode, "FORCE", sdslen(mode)))
		{
			oc_cmd=TB_CMD_CONSUME_FORCE;
		}
		else if(0==strncasecmp(mode, "FLEXIBLE", sdslen(mode)))
		{
			oc_cmd=TB_CMD_CONSUME_FLEXIBLE;
		}
		else
		{
			*reply=swarmkv_reply_new_error(error_arg_string_should_be, mode, "NORMAL|FORCE|FLEXIBLE");
			return FINISHED;
		}
	}
	got=OC_token_bucket_consume(obj->bucket, now, oc_cmd, request);
	*reply=swarmkv_reply_new_integer(got);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
