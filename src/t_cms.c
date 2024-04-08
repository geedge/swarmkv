#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "cm_sketch.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result cmsinitbydim_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* CMSINITBYDIM key width depth*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

    long long width=0, depth=0;

	width=strtol(cmd->argv[2], NULL, 10);
    if(width<=0)
    {
        *reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
        return FINISHED;
    }
    depth=strtol(cmd->argv[3], NULL, 10);
    if(depth<=0)
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
		assert(obj->raw==NULL);
        uuid_t uuid;
        store_get_uuid(mod_store, uuid);
        obj->cms=CM_sketch_new(uuid, width, depth);
		obj->type=OBJ_TYPE_CMS;
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_array(0);
	}
	return FINISHED;
}
enum cmd_exec_result cmsinitbyprob_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* CMSINITBYPROB key error probability*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];

    double error=0, probability=0;
	error=strtod(cmd->argv[2], NULL);
	if(error < 0 || error >= 1.0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_float, cmd->argv[2]);
		return FINISHED;
	}
	probability=strtod(cmd->argv[3], NULL);
	if(probability < 0 || probability >= 1.0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_float, cmd->argv[3]);
		return FINISHED;
	}
	int width=0, depth=0;
	CM_sketch_tool_dimension_by_prob(error, probability, &width, &depth);
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
        uuid_t uuid;
        store_get_uuid(mod_store, uuid);
        obj->cms=CM_sketch_new(uuid, width, depth);
		obj->type=OBJ_TYPE_CMS;
		*reply=swarmkv_reply_new_status("OK");
	}
	else
	{
		*reply=swarmkv_reply_new_array(0);
	}
	return FINISHED;
}
static int parse_incrby_args(const struct swarmkv_cmd *cmd, long long *increments, size_t n_increment, int *invalid_idx)
{
    assert(n_increment == (cmd->argc-2)/2);
    char *endptr=NULL;
    for(int i=3, j=0; i<cmd->argc; i+=2, j++)
    {
        *invalid_idx=i;
        increments[j]=strtol(cmd->argv[i], &endptr, 10);
        if(*endptr != '\0')
        {
            return -1;
        }
        if(increments[j] > INT32_MAX || increments[j] < INT32_MIN)
        {
            return -1;
        }
    }
    return 0;
}
enum cmd_exec_result cmsincrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CMSINCRBY key item increment [item increment ...]*/
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
		assert(obj->raw == NULL);
		return handle_undefined_object(obj, reply);
	}
	else if(obj->type!=OBJ_TYPE_CMS)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
    size_t n_increment=(cmd->argc-2)/2;
    long long increments[n_increment], query[n_increment];
    int ret=0, invalid_idx=0;
    ret = parse_incrby_args(cmd, increments, n_increment, &invalid_idx);
    if(ret<0)
    {
        *reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[invalid_idx]);
        return FINISHED;
    }
    *reply=swarmkv_reply_new_array(n_increment);
	for(int i=2, j=0; i<cmd->argc; i+=2, j++)
	{
        query[j]=CM_sketch_incrby(obj->cms, cmd->argv[i], sdslen(cmd->argv[i]), (int) increments[j]);
       (*reply)->elements[j]=swarmkv_reply_new_integer(query[j]);
	}
	store_mark_object_as_modified(mod_store, obj);
	return FINISHED;
}
enum cmd_exec_result cmsmquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CMSMQUERY key item [item ...]*/
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
	else if(obj->type!=OBJ_TYPE_CMS)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}

    *reply=swarmkv_reply_new_array(cmd->argc-2);
	for(int i=0; i<cmd->argc-2; i++)
	{
        int query=0;
        query=CM_sketch_query(obj->cms, cmd->argv[i+2], sdslen(cmd->argv[i+2]));
       (*reply)->elements[i]=swarmkv_reply_new_integer(query);
	}
	return FINISHED;
}
enum cmd_exec_result cmsquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    enum cmd_exec_result ret;
    struct swarmkv_reply *tmp_reply=NULL;
    ret=cmsmquery_command(mod_store, cmd, &tmp_reply);
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
enum cmd_exec_result cmsinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CMSINFO key*/
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
	else if(obj->type!=OBJ_TYPE_CMS)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct CM_sketch_info info;
	CM_sketch_info(obj->cms, &info);
	int i=0;
	*reply=swarmkv_reply_new_array(12);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Width");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.width);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Depth");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.depth);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Error");
	(*reply)->elements[i++]=swarmkv_reply_new_double(info.error);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Probability");
	(*reply)->elements[i++]=swarmkv_reply_new_double(info.probability);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Count");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.count);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("ReplicaNumber");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(info.n_replica);
	assert(i==12);
	return FINISHED;
}
enum cmd_exec_result cmsrlist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CMSRLIST key*/
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
	else if(obj->type!=OBJ_TYPE_CMS)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	struct CM_sketch_info info;
	CM_sketch_info(obj->cms, &info);
	uuid_t replicas[info.n_replica];
	size_t n_nodes=0;
	CM_sketch_list_replica(obj->cms, replicas, &n_nodes);
	*reply=swarmkv_reply_new_array(n_nodes);
	for(int i=0; i<n_nodes; i++)
	{
		char uuid_str[37];
		uuid_unparse(replicas[i], uuid_str);
		(*reply)->elements[i]=swarmkv_reply_new_string(uuid_str, strlen(uuid_str));
	}
	return FINISHED;
}
enum cmd_exec_result cmsrclear_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CMSRCLEAR key uuid*/
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
	else if(obj->type!=OBJ_TYPE_CMS)
	{
		*reply=swarmkv_reply_new_error(error_wrong_type);		
		return FINISHED;
	}
	uuid_t uuid;
	if(0 > uuid_parse(cmd->argv[2], uuid))
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_uuid, cmd->argv[2]);
		return FINISHED;
	}
	int ret=CM_sketch_clear_replica(obj->cms, uuid);
	if(ret<0)
	{
		*reply=swarmkv_reply_new_error("replica uuid is not exist");
	}
	else
	{
		*reply=swarmkv_reply_new_status("OK");
		store_mark_object_as_modified(mod_store, obj);
	}	
	return FINISHED;
}