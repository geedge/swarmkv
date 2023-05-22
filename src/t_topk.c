#include "t_topk.h"

#include <assert.h>
#include <stdlib.h>

#include "swarmkv_common.h"
#include "swarmkv_error.h"
#include "swarmkv_store.h"

// TOPKREV key K [width] [depth] [decay rate]
enum cmd_exec_result topkrev_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply) {
    if (cmd->argc - 1 < 2 || cmd->argc > 5) {
        *reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return FINISHED;
    }

    const sds key = cmd->argv[1]; 
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj) {
        // TODO: 这种情况下是不是也应该创建一个hk
        return NEED_KEY_ROUTE;
    }

    if (obj->type != OBJ_TYPE_UNDEFINED) {
        *reply = swarmkv_reply_new_error(error_wrong_type); 
        // TODO: 如果是OBJ_TYPE_TOPK 类型，是不是提示信息应该不一样
        return FINISHED;
    }

    // TODO: 封个函数
    char *endptr=NULL;
    int K = strtol(cmd->argv[2], &endptr, 10);
    if(*endptr!='\0' || K <= 0) {
        *reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
        return FINISHED;
    }

    struct heavy_keeper_options params = {0};
    int arg_num = cmd->argc - 1;
    if (arg_num >= 3) {
        int width=strtol(cmd->argv[3], &endptr, 10);
        if(*endptr!='\0' || width <= 0)
        {
            *reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
            return FINISHED; 		
        }
        params.array_num = width;
    }
    if (arg_num >= 4) {
        int depth = strtol(cmd->argv[4], &endptr, 10);
        if(*endptr!='\0' || depth <= 0) {
            *reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
            return FINISHED;
        }
        params.max_bucket_num = depth;
    }
    if (arg_num == 5) {
        double decay_rate = strtod(cmd->argv[5], &endptr);
        if(*endptr!='\0' || decay_rate <= 1) {
            *reply=swarmkv_reply_new_error(error_arg_not_in_range, cmd->argv[5]);
            return FINISHED;
        }
        params.decay_exp_rate = decay_rate;
    }

    obj->topk = heavy_keeper_new(K, &params);
    *reply=swarmkv_reply_new_status("OK");

    return FINISHED;
}

// TOPKADD key member score
enum cmd_exec_result topkadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply) {
    // TODO: ADD "1" 10 "2" 3 "4" [1]的支持
    if (cmd->argc - 1 != 3) {
        *reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return FINISHED;
    }

    char *endptr=NULL;
	long long score = strtol(cmd->argv[3], &endptr, 10);
	if(*endptr!='\0' || score<0)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
		return FINISHED; 		
	}

    const sds key = cmd->argv[1]; 
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj) {
        *reply = swarmkv_reply_new_error(error_no_heavy_keeper_initialized);
        // TODO: 可能应该返回NEED_KEY_ROUTE, 每个key的来源不是用户吗
        return FINISHED;
    }

    if (obj->type != OBJ_TYPE_TOPK) {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    heavy_keeper_add(obj->topk, cmd->argv[2], sdslen(cmd->argv[2]), (unsigned int)score);
    *reply=swarmkv_reply_new_status("OK");

    sobj_need_sync(mod_store, obj);

    return FINISHED;
}

// TOPKINFO key
enum cmd_exec_result topkinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply) {
    if (cmd->argc - 1 != 1) {
        *reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return FINISHED;
    }

    const sds key = cmd->argv[1]; 
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj) {
        *reply = swarmkv_reply_new_error(error_no_heavy_keeper_initialized);
        // TODO: 可能应该返回NEED_KEY_ROUTE
        return FINISHED;
    }

    if (obj->type == OBJ_TYPE_UNDEFINED) {
        *reply = swarmkv_reply_new_error(error_no_heavy_keeper_initialized);
        return FINISHED;
    }

    if (obj->type != OBJ_TYPE_TOPK) {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    struct heavy_keeper *topk = obj->topk;
    struct heavy_keeper_options params;
    int K;
    heavy_keeper_get_params(topk, &params, &K);
    *reply=swarmkv_reply_new_string_fmt("parameters: K: %d, depth=%d, max_bucket_num=%d, decay_exp_rate=%f\n",
                                        K, params.array_num, params.max_bucket_num, params.decay_exp_rate);

    return FINISHED;
}

// TOPKLIST key
enum cmd_exec_result topklist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply) {
    if (cmd->argc - 1 != 1) {
        *reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return FINISHED;
    }

    const sds key = cmd->argv[1]; 
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj) {
        *reply = swarmkv_reply_new_error(error_no_heavy_keeper_initialized);
        // TODO: 可能应该返回NEED_KEY_ROUTE
        return FINISHED;
    }

    if (obj->type == OBJ_TYPE_UNDEFINED) {
        *reply = swarmkv_reply_new_error(error_no_heavy_keeper_initialized);
        return FINISHED;
    }

    if (obj->type != OBJ_TYPE_TOPK) {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    struct heavy_keeper *topk = obj->topk;
    struct heavy_keeper_result *result = heavy_keeper_query(topk);

    size_t n_to_get = result->n_key;
    *reply=swarmkv_reply_new_array(n_to_get);
    for(size_t i=0; i<n_to_get; i++)
    {
        (*reply)->elements[i]=swarmkv_reply_new_string_fmt("%s %u", result->key, result->count);
    }
	return FINISHED;    
}

// TOPKQUERY key member return 0 for not in, 1 for in
enum cmd_exec_result topkquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{    
    // TODO
    return FINISHED;
}