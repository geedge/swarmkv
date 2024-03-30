#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "ap_bloom.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result bfinit_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* BFINIT key error_rate capacity [TIME window-milliseconds slice-number] */
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

    double error_rate=0;
    long long capacity=0, time_window_ms=0, time_slice_num=0;

	int ret=0;
	error_rate=strtod(cmd->argv[2], NULL);
	if(error_rate < 0 || error_rate >= 1.0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_float, cmd->argv[2]);
		return FINISHED;
	}
	ret=str2integer(cmd->argv[3], &capacity);
    if(ret<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
	}
	if(cmd->argc==7)
	{
		if(strncasecmp(cmd->argv[4], "TIME", 4)!=0)
		{
			*reply=swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[4], "TIME");
			return FINISHED;
		}
		ret=str2integer(cmd->argv[5], &time_window_ms);
		if(ret<0)
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[5]);
			return FINISHED;
		}
		ret=str2integer(cmd->argv[6], &time_slice_num);
		if(ret<0 || time_slice_num<0)
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[6]);
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
		assert(obj->raw==NULL);
        obj->bloom=AP_bloom_new(now, error_rate, capacity, time_window_ms, time_slice_num);
		obj->type=OBJ_TYPE_BLOOM_FILTER;
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_array(0);
	}
	return FINISHED;
}
enum cmd_exec_result bfadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*BFADD key item [item ...]*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;	
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	else if(obj->type!=OBJ_TYPE_BLOOM_FILTER)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}

	for(int i=0; i<cmd->argc-2; i++)
	{
		AP_bloom_add(obj->bloom, now, cmd->argv[i+2], sdslen(cmd->argv[i+2]));
	}
	store_mark_object_as_modified(mod_store, obj);
	*reply=swarmkv_reply_new_status("OK");
	return FINISHED;
}
enum cmd_exec_result bfmexists_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*BFMEXISTS key item [item ...]*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;	
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	else if(obj->type!=OBJ_TYPE_BLOOM_FILTER)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}

	long long exists=0;
	*reply=swarmkv_reply_new_array(cmd->argc-2);
	for(int i=0; i<cmd->argc-2; i++)
	{
		exists = AP_bloom_check(obj->bloom, now, cmd->argv[i+2], sdslen(cmd->argv[i+2]));
		(*reply)->elements[i]=swarmkv_reply_new_integer(exists);
	}
	return FINISHED;
}
enum cmd_exec_result bfexists_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    enum cmd_exec_result ret;
    struct swarmkv_reply *tmp_reply=NULL;
    ret=bfmexists_command(mod_store, cmd, &tmp_reply);
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
enum cmd_exec_result bfcard_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*BFCARD key*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;	
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	else if(obj->type!=OBJ_TYPE_BLOOM_FILTER)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}

	long long cardinality=0;
	cardinality=AP_bloom_cardinality(obj->bloom);
	*reply=swarmkv_reply_new_integer(cardinality);
	return FINISHED;
}
enum cmd_exec_result bfinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*BFINFO key*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;	
	}
	struct timeval now;
	gettimeofday(&now, NULL);
	if(obj->type==OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	else if(obj->type!=OBJ_TYPE_BLOOM_FILTER)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct AP_bloom_info info;
	AP_bloom_info(obj->bloom, &info);
	int i=0;
	*reply=swarmkv_reply_new_array(20);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Error");
	(*reply)->elements[i++]=swarmkv_reply_new_double(info.error);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Capacity");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.capacity);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("TimeWindowMs");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.time_window_ms);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("TimeSlices");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.time_slice_num);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("HashNum");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.hash_num);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("TotalSlices");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.total_slice_number);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("MaxExpansionTimes");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.max_expand_times);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("ApproximateItemNum");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.approximate_item_num);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("FillRatio");
	(*reply)->elements[i++]=swarmkv_reply_new_double(info.fill_ratio);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("OldestItemTime");
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("%lld.%03lld", info.oldest_item_time.tv_sec, info.oldest_item_time.tv_usec/1000);
	assert(i==20);
	return FINISHED;
}