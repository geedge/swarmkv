#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "swarmkv_utils.h"
#include "utarray.h"

#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
//Unlike string, set and hash, XTCONSUME and XTINFO can only operate on an initialized token bucket.
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
static int get_consume_type(sds s, enum tb_consume_type *consume_type)
{
	if(0==strncasecmp(s, "NORMAL", sdslen(s)))
	{
		*consume_type=TB_CONSUME_NORMAL;
	}
	if(0==strncasecmp(s, "FORCE", sdslen(s)))
	{
		*consume_type=TB_CONSUME_FORCE;
	}
	else if(0==strncasecmp(s, "FLEXIBLE", sdslen(s)))
	{
		*consume_type=TB_CONSUME_FLEXIBLE;
	}
	else
	{
		return -1;
	}
	return 0;
}
enum cmd_exec_result tcfg_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TCFG key rate capacity*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	char *endptr=NULL;

	long long rate=0, capacity=0;
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
		obj->bucket=OC_token_bucket_new(uuid, now, rate, capacity);
		obj->type=OBJ_TYPE_TOKEN_BUCKET;
		*reply=swarmkv_reply_new_status("OK");
	}
	else if(obj->type==OBJ_TYPE_TOKEN_BUCKET)
	{
		OC_token_bucket_configure(obj->bucket, now, rate, capacity);
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
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if(obj->type!=OBJ_TYPE_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}

	struct timeval now;
	gettimeofday(&now, NULL);
	
	
	struct OC_token_bucket_info oc_info;
	memset(&oc_info, 0, sizeof(oc_info));
	OC_token_bucket_info(obj->bucket, now, &oc_info);
	int i=0;
	*reply=swarmkv_reply_new_array(10);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.CIR);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.CBS);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Consumed");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.consumed);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Refilled");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.refilled);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Available");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(oc_info.available);
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

	long long request=0, allocated=0;
	char *endptr=NULL;
	request=strtol(cmd->argv[2], &endptr, 10);
	if(*endptr!='\0' || request<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}

	enum tb_consume_type consume_type=TB_CONSUME_NORMAL;
	if(cmd->argc>3)
	{
		if(0>get_consume_type(cmd->argv[3], &consume_type))
		{
			*reply=swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[3], "NORMAL|FORCE|FLEXIBLE");
			return FINISHED;
		}
	}

	if(obj->type!=OBJ_TYPE_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	allocated=OC_token_bucket_consume(obj->bucket, now, consume_type, request);
	*reply=swarmkv_reply_new_integer(allocated);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
bool is_power_of_2(long long num) 
{
    if (num > 0 && (num & (num - 1)) == 0) {
        return true;
    }
    return false;
}
enum cmd_exec_result ftcfg_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*FTCFG key rate capacity divisor*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	char *endptr=NULL;

	long long rate=0, capacity=0, divisor=0;
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
	divisor=strtol(cmd->argv[4], &endptr, 10);
	if(*endptr!='\0' || !is_power_of_2(divisor))
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
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
	
		obj->type=OBJ_TYPE_FAIR_TOKEN_BUCKET;
		obj->ftb=fair_token_bucket_new(uuid, now, rate, capacity, divisor);
		*reply=swarmkv_reply_new_status("OK");
	}
	else if(obj->type==OBJ_TYPE_FAIR_TOKEN_BUCKET)
	{
		fair_token_bucket_configure(obj->ftb, now, rate, capacity, divisor);
		sobj_need_sync(mod_store, obj);
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;
}
enum cmd_exec_result ftconsume_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*FTCONSUME key member weight tokens*/	
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	const sds member=cmd->argv[2];
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
	request=strtol(cmd->argv[4], &endptr, 10);
	if(*endptr!='\0' || request<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
		return FINISHED;
	}
	long long weight=0;
	weight=strtol(cmd->argv[3], &endptr, 10);
	if(*endptr!='\0' || weight<1 || weight>20)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
		return FINISHED;
	}
	if(obj->type!=OBJ_TYPE_FAIR_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	allocated=fair_token_bucket_consume(obj->ftb, now, member, sdslen(member), weight, TB_CONSUME_NORMAL, request);

	*reply=swarmkv_reply_new_integer(allocated);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
enum cmd_exec_result ftinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*FTINFO key*/	
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
	if(obj->type!=OBJ_TYPE_FAIR_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);

	struct fair_token_bucket_info ftb_info;
	memset(&ftb_info, 0, sizeof(ftb_info));
	fair_token_bucket_info(obj->ftb, now, &ftb_info);
	
	int i=0;
	*reply=swarmkv_reply_new_array(14);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.CIR);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.CBS);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Consumed");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.consumed);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Refilled");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.refilled);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Available");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.bucket_info.available);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Divisor");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.divisor);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("ActiveMembers");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ftb_info.active_key_number);
	assert(i==14);
	return FINISHED;
}
enum cmd_exec_result btcfg_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*BTCFG key rate capacity buckets*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	
	char *endptr=NULL;

	long long rate=0, capacity=0, buckets=0;
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
	buckets=strtol(cmd->argv[4], &endptr, 10);
	if(*endptr!='\0' || !is_power_of_2(buckets))
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
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
	
		obj->type=OBJ_TYPE_BULK_TOKEN_BUCKET;
		obj->btb=bulk_token_bucket_new(uuid, now, rate, capacity, buckets);
		*reply=swarmkv_reply_new_status("OK");
	}
	else if(obj->type==OBJ_TYPE_BULK_TOKEN_BUCKET)
	{
		bulk_token_bucket_configure(obj->btb, now, rate, capacity, buckets);
		sobj_need_sync(mod_store, obj);
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
	}	
	return FINISHED;
}
enum cmd_exec_result btconsume_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*BTCONSUME key member tokens [NORMAL|FORCE|FLEXIBLE]*/	
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	const sds member=cmd->argv[2];
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
	request=strtol(cmd->argv[3], &endptr, 10);
	if(*endptr!='\0' || request<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
		return FINISHED;
	}

	enum tb_consume_type consume_type=TB_CONSUME_NORMAL;
	if(cmd->argc>4)
	{
		if(0>get_consume_type(cmd->argv[4], &consume_type))
		{
			*reply=swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[4], "NORMAL|FORCE|FLEXIBLE");
			return FINISHED;
		}
	}

	if(obj->type!=OBJ_TYPE_BULK_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);	
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	allocated=bulk_token_bucket_consume(obj->btb, now, member, sdslen(member), consume_type, request);

	*reply=swarmkv_reply_new_integer(allocated);
	sobj_need_sync(mod_store, obj);
	return FINISHED;
}
enum cmd_exec_result btinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*BTINFO key [member]*/	
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
	if(obj->type!=OBJ_TYPE_BULK_TOKEN_BUCKET)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	struct timeval now;
	gettimeofday(&now, NULL);

	struct bulk_token_bucket_info btb_info;
	memset(&btb_info, 0, sizeof(btb_info));
	bulk_token_bucket_info(obj->btb, now, &btb_info);
	long long available=-1;
	if(cmd->argc>2)
	{
		available=bulk_token_bucket_read_available(obj->btb, now, cmd->argv[2], sdslen(cmd->argv[2]));
	}
	int i=0;
	*reply=swarmkv_reply_new_array(12);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Rate");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(btb_info.CIR);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(btb_info.CBS);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Buckets");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(btb_info.bucket_number);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("ActiveMembers");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(btb_info.estimate_keys);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Collisions");
	(*reply)->elements[i++]=swarmkv_reply_new_double(btb_info.collision_rate);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Query");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(available);
	assert(i==12);
	return FINISHED;
}