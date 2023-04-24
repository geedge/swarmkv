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
/*TCFG key rate capacity [FAIR [divisor] | BULK [buckets] ]*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	char *endptr=NULL;

	long long capacity=0, rate=0;
	enum sobj_type desire_type=OBJ_TYPE_TOKEN_BUCKET;
	rate=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0' || rate<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED; 		
	}

	capacity=strtol(cmd->argv[3], &endptr, 10);
	if(*endptr!='\0' || capacity<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
		return FINISHED; 		
	}
	
	if(cmd->argc>4)
	{
		if(0==strncasecmp(cmd->argv[4], "FAIR", sdslen(cmd->argv[4])))
		{
			desire_type=OBJ_TYPE_FAIR_TOKEN_BUCKET;
		}
		else if (0==strncasecmp(cmd->argv[4], "BULK", sdslen(cmd->argv[4])))
		{
			desire_type=OBJ_TYPE_BULK_TOKEN_BUCKET;
		}
	}
	long long divisor_or_buckets=8192;
	if(cmd->argc>5)
	{
		divisor_or_buckets=strtol(cmd->argv[3], &endptr, 10);
		if(*endptr!='\0' || divisor_or_buckets<0)
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
			return FINISHED; 		
		}
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
	
		obj->type=desire_type;
		switch(obj->type)
		{
			case OBJ_TYPE_TOKEN_BUCKET:
				obj->bucket=OC_token_bucket_new(uuid, now, rate, capacity);
				break;
			case OBJ_TYPE_FAIR_TOKEN_BUCKET:
				obj->ftb=fair_token_bucket_new(uuid, now, rate, capacity, divisor_or_buckets);
				break;
			case OBJ_TYPE_BULK_TOKEN_BUCKET:
				obj->btb=bulk_token_bucket_new(uuid, now, rate, capacity, divisor_or_buckets);
				break;
			default:
				assert(0);
				break;
		}
		*reply=swarmkv_reply_new_status("OK");
	}
	else if(obj->type==desire_type)
	{
		switch(obj->type)
		{
			case OBJ_TYPE_TOKEN_BUCKET:
				OC_token_bucket_configure(obj->bucket, now, rate, capacity);
				break;
			case OBJ_TYPE_FAIR_TOKEN_BUCKET:
				fair_token_bucket_configure(obj->ftb, now, rate, capacity, divisor_or_buckets);
				break;
			case OBJ_TYPE_BULK_TOKEN_BUCKET:
				bulk_token_bucket_configure(obj->btb, now, rate, capacity, divisor_or_buckets);
				break;
			default:
				assert(0);
				break;
		}
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

	struct OC_token_bucket_info oc_info;
	struct fair_token_bucket_info ftb_info;
	struct bulk_token_bucket_info btb_info;
	struct timeval now;
	gettimeofday(&now, NULL);
	int i=0;
	switch(obj->type)
	{
		case OBJ_TYPE_TOKEN_BUCKET:
			memset(&oc_info, 0, sizeof(oc_info));
			OC_token_bucket_info(obj->bucket, now, &oc_info);
			*reply=swarmkv_reply_new_array(10);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.CBS);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.CIR);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Consumed");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.consumed);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Refilled");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.refilled);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Available");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.available);
			assert(i==10);
			break;
		case OBJ_TYPE_FAIR_TOKEN_BUCKET:
			memset(&ftb_info, 0, sizeof(ftb_info));
			fair_token_bucket_info(obj->ftb, now, &ftb_info);
			*reply=swarmkv_reply_new_array(12);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.CBS);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.CIR);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Consumed");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.consumed);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Refilled");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.refilled);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Available");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.available);
			(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("ActiveMembers");
			(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.active_key_number);
			assert(i==12);
			break;
		case OBJ_TYPE_BULK_TOKEN_BUCKET:
			memset(&btb_info, 0, sizeof(btb_info));
			bulk_token_bucket_info(obj->btb, now, &btb_info);
			break;
		default:
			*reply=swarmkv_reply_new_error(error_wrong_type);		
			break;
	}
	return FINISHED;
}
enum cmd_exec_result tconsume_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TCONSUME key tokens [NORMAL|FORCE|FLEXIBLE]*/
/*TCONSUME key tokens [MEMBER member] [WEIGHT weight] [TYPE NORMAL|FORCE|FLEXIBLE]*/	
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

	long long request=0, allocated=0;
	char *endptr=NULL;
	request=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0' || request<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}

	sds member=NULL;
	long long weight=-1;
	enum tb_consume_type consume_type=TB_CONSUME_NORMAL;
	int i=3;
	while(i+1<cmd->argc)
	{
		if(0==strncasecmp(cmd->argv[i], "MEMBER", sdslen(cmd->argv[i])))
		{
			member=cmd->argv[i+1];
			i++;
		}
		else if(0==strncasecmp(cmd->argv[i], "WEIGHT", sdslen(cmd->argv[i])))
		{
			weight=strtol(cmd->argv[i+1], &endptr, 10);
			if(*endptr!='\0' || weight<1 || weight>20)
			{
				*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[i+1]);
				return FINISHED;
			}
			i++;
		}
		else if(0==strncasecmp(cmd->argv[i], "TYPE", sdslen(cmd->argv[i])))
		{
			if(0==strncasecmp(cmd->argv[i+1], "NORMAL", sdslen(cmd->argv[i+1])))
			{
				consume_type=TB_CONSUME_NORMAL;
			}
			if(0==strncasecmp(cmd->argv[i+1], "FORCE", sdslen(cmd->argv[i+1])))
			{
				consume_type=TB_CONSUME_FORCE;
			}
			else if(0==strncasecmp(cmd->argv[i+1], "FLEXIBLE", sdslen(cmd->argv[i+1])))
			{
				consume_type=TB_CONSUME_FLEXIBLE;
			}
			else
			{
				*reply=swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[i+1], "NORMAL|FORCE|FLEXIBLE");
				return FINISHED;
			}
			i++;
		}
		i++;
	}
	enum sobj_type desire_type=OBJ_TYPE_TOKEN_BUCKET;
	if(weight>0 && member)
	{
		desire_type=OBJ_TYPE_FAIR_TOKEN_BUCKET;
	}
	else if(member)
	{
		desire_type=OBJ_TYPE_BULK_TOKEN_BUCKET;
	}
	else
	{
		desire_type=OBJ_TYPE_TOKEN_BUCKET;
	}
	if(obj->type!=desire_type)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	switch(obj->type)
	{
		case OBJ_TYPE_TOKEN_BUCKET:
			allocated=OC_token_bucket_consume(obj->bucket, now, consume_type, request);
			break;
		case OBJ_TYPE_FAIR_TOKEN_BUCKET:
			allocated=fair_token_bucket_consume(obj->ftb, now, member, sdslen(member), weight, consume_type, request);
			break;
		case OBJ_TYPE_BULK_TOKEN_BUCKET:
			allocated=bulk_token_bucket_consume(obj->btb, now, member, sdslen(member), consume_type, request);
			break;
		default:
			assert(0);
			break;
	}
	*reply=swarmkv_reply_new_integer(allocated);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
