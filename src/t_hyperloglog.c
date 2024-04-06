#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "st_hyperloglog.h"

#include <stdlib.h>
#include <assert.h>
enum cmd_exec_result pfinit_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* PFINIT key precison [TIME window-milliseconds] */
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

    long long precision=0, time_window_ms=0;


	int ret=0;
	precision=strtod(cmd->argv[2], NULL);

	if(precision < HLL_MIN_PRECISION || precision > HLL_MAX_PRECISION)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	if(cmd->argc==5)
	{
		if(strncasecmp(cmd->argv[3], "TIME", 4)!=0)
		{
			*reply=swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[4], "TIME");
			return FINISHED;
		}
		ret=str2integer(cmd->argv[4], &time_window_ms);
		if(ret<0)
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[5]);
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
        obj->hll=ST_hyperloglog_new(precision, time_window_ms, now);
		obj->type=OBJ_TYPE_HYPERLOGLOG;
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_array(0);
	}
	return FINISHED;
}
enum cmd_exec_result pfadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*PFADD key item [item ...]*/
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
	else if(obj->type!=OBJ_TYPE_HYPERLOGLOG)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
    int at_least_one_register_altered=0;
	for(int i=0; i<cmd->argc-2; i++)
	{
        int ret=0;
        ret=ST_hyperloglog_add(obj->hll, cmd->argv[i+2], sdslen(cmd->argv[i+2]), now);
        if(ret)
        {
            at_least_one_register_altered=1;
        }
	}
	store_mark_object_as_modified(mod_store, obj);
    *reply=swarmkv_reply_new_integer(at_least_one_register_altered);
	return FINISHED;
}
enum cmd_exec_result pfcount_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*PFCOUNT key*/
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
	else if(obj->type!=OBJ_TYPE_HYPERLOGLOG)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}

	long long count=0;
    count=ST_hyperloglog_count(obj->hll, now);
	*reply=swarmkv_reply_new_integer(count);
	return FINISHED;
}
enum cmd_exec_result pfinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*PFINFO key*/
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
	else if(obj->type!=OBJ_TYPE_HYPERLOGLOG)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct ST_hyperloglog_info info;
	ST_hyperloglog_info(obj->hll, &info);
	int i=0;
	*reply=swarmkv_reply_new_array(6);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Error");
    (*reply)->elements[i++]=swarmkv_reply_new_double(info.error);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Precision");
    (*reply)->elements[i++]=swarmkv_reply_new_integer(info.precision);
    (*reply)->elements[i++]=swarmkv_reply_new_string_fmt("TimeWindowMs");
    (*reply)->elements[i++]=swarmkv_reply_new_integer(info.time_window_ms);
    assert(i==6);
	return FINISHED;
}