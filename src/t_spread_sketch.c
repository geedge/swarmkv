#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "spread_sketch.h"

enum cmd_exec_result ssinitbydim_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    /* SSINITBYDIM key width depth precision [TIME window-milliseconds]*/
    const sds key = cmd->argv[1];

    long width = strtol(cmd->argv[2], NULL, 10);
    if (width <= 0)
    {
        *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
        return FINISHED;
    }
    long depth = strtol(cmd->argv[3], NULL, 10);
    if (depth <= 0)
    {
        *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[3]);
        return FINISHED;
    }
    unsigned char precision = (unsigned char)strtol(cmd->argv[4], NULL, 10);
    if (precision <= 0)
    {
        *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
        return FINISHED;
    }
    long long time_window_ms = 0;
    if (cmd->argc == 7)
    {
        if (strncasecmp(cmd->argv[5], "TIME", 4) != 0)
        {
            *reply = swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[5], "TIME");
            return FINISHED;
        }
        if (str2integer(cmd->argv[6], &time_window_ms) < 0)
        {
            *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[6]);
            return FINISHED;
        }
    }

    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }

    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        assert(obj->raw == NULL);

        struct timeval now;
        gettimeofday(&now, NULL);
        obj->spread_sketch = spread_sketch_new(width, depth, precision, time_window_ms, now);

        obj->type = OBJ_TYPE_SPREAD_SKETCH;
        *reply = swarmkv_reply_new_status("OK");
    }
    else
    {
        *reply = swarmkv_reply_new_array(0);
    }
    return FINISHED;
}

enum cmd_exec_result ssinitbycapacity_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    /* SSINITBYCAPACITY key capacity precision [TIME window-milliseconds]*/
    const sds key = cmd->argv[1];
    long expected_superspreader_count = strtol(cmd->argv[2], NULL, 10);
    if (expected_superspreader_count <= 0)
    {
        *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
        return FINISHED;
    }
    long long time_window_ms = 0; // when set to 0, it means no time decay is used
    if (cmd->argc == 5)
    {
        if (strncasecmp(cmd->argv[3], "TIME", 4) != 0)
        {
            *reply = swarmkv_reply_new_error(error_arg_string_should_be, cmd->argv[3], "TIME");
            return FINISHED;
        }
        if (str2integer(cmd->argv[4], &time_window_ms) < 0)
        {
            *reply = swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[4]);
            return FINISHED;
        }
    }

    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }

    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        assert(obj->raw == NULL);

        struct timeval now;
        gettimeofday(&now, NULL);

        int width, depth;
        unsigned char precision;
        spread_sketch_recommend_parameters(expected_superspreader_count, &width, &depth, &precision);
        obj->spread_sketch = spread_sketch_new(width, depth, precision, time_window_ms, now);

        obj->type = OBJ_TYPE_SPREAD_SKETCH;
        *reply = swarmkv_reply_new_status("OK");
    }
    else
    {
        *reply = swarmkv_reply_new_array(0);
    }
    return FINISHED;
}

enum cmd_exec_result ssadd_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    // SSADD key entry item [item ...]
    const sds key = cmd->argv[1];
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }
    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        return handle_undefined_object(obj, reply);
    }
    if (obj->type != OBJ_TYPE_SPREAD_SKETCH)
    {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    const sds entry_key = cmd->argv[2];

    struct timeval now;
    gettimeofday(&now, NULL);
    for (size_t i = 3; i < cmd->argc; i++)
    {
        spread_sketch_add(obj->spread_sketch, entry_key, sdslen(entry_key), cmd->argv[i], sdslen(cmd->argv[i]), NULL, now);
    }

    double est = spread_sketch_query(obj->spread_sketch, entry_key, sdslen(entry_key), now);
    *reply = swarmkv_reply_new_integer((int)(est + 0.5));

    store_mark_object_as_modified(mod_store, obj);
    return FINISHED;
}

enum cmd_exec_result sslist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    // SSLIST key
    const sds key = cmd->argv[1];

    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }
    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        return handle_undefined_object(obj, reply);
    }
    if (obj->type != OBJ_TYPE_SPREAD_SKETCH)
    {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    struct spread_sketch *ss = obj->spread_sketch;
    size_t n_entry;
    char **entry_keys = NULL;
    size_t *entry_keys_len = NULL;
    spread_sketch_list_keys(ss, &entry_keys, &entry_keys_len, &n_entry);

    *reply = swarmkv_reply_new_array(n_entry * 2);
    for (size_t i = 0; i < n_entry; i++)
    {
        const char *entry_tmp = entry_keys[i];
        size_t len_tmp = entry_keys_len[i];
        int est_tmp = (int)(spread_sketch_get_cardinality(ss, entry_tmp, len_tmp) + 0.5);

        (*reply)->elements[i * 2] = swarmkv_reply_new_string(entry_tmp, len_tmp);
        (*reply)->elements[i * 2 + 1] = swarmkv_reply_new_integer(est_tmp);
    }

    free(entry_keys);
    free(entry_keys_len);
    return FINISHED;
}

enum cmd_exec_result ssmquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    // SSMQUERY key entry [entry ...]
    const sds key = cmd->argv[1];
    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }
    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        return handle_undefined_object(obj, reply);
    }
    if (obj->type != OBJ_TYPE_SPREAD_SKETCH)
    {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    struct spread_sketch *ss = obj->spread_sketch;
    struct timeval now;
    gettimeofday(&now, NULL);
    *reply = swarmkv_reply_new_array(cmd->argc - 2);
    for (size_t i = 2; i < cmd->argc; i++)
    {
        const sds entry_key = cmd->argv[i];
        double est = spread_sketch_query(ss, entry_key, sdslen(entry_key), now);
        (*reply)->elements[i - 2] = swarmkv_reply_new_integer((int)(est + 0.5));
    }
    return FINISHED;
}
enum cmd_exec_result ssquery_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    // SSQUERY key entry
    enum cmd_exec_result ret;
    struct swarmkv_reply *tmp_reply = NULL;
    ret = ssmquery_command(mod_store, cmd, &tmp_reply);
    if (ret == FINISHED)
    {
        if (tmp_reply->type == SWARMKV_REPLY_ARRAY)
        {
            *reply = swarmkv_reply_dup(tmp_reply->elements[0]);
            swarmkv_reply_free(tmp_reply);
        }
        else
        {
            *reply = tmp_reply;
        }
    }
    return ret;
}
enum cmd_exec_result ssinfo_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    // SSINFO key
    const sds key = cmd->argv[1];

    struct sobj *obj = store_lookup(mod_store, key);
    if (!obj)
    {
        return NEED_KEY_ROUTE;
    }
    if (obj->type == OBJ_TYPE_UNDEFINED)
    {
        return handle_undefined_object(obj, reply);
    }
    if (obj->type != OBJ_TYPE_SPREAD_SKETCH)
    {
        *reply = swarmkv_reply_new_error(error_wrong_type);
        return FINISHED;
    }

    struct spread_sketch *ss = obj->spread_sketch;
    int depth, width, time_window_ms;
    unsigned char precision;
    spread_sketch_get_parameter(ss, &depth, &width, &precision, &time_window_ms);
    double error_cms = 2.0 / (double)width;
    double error_hll = ST_hyperloglog_error_for_precision(precision);
    double probability = 1 / pow(2, depth);

    int i = 0;
    *reply = swarmkv_reply_new_array(14);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("Width");
    (*reply)->elements[i++] = swarmkv_reply_new_integer(width);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("Depth");
    (*reply)->elements[i++] = swarmkv_reply_new_integer(depth);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("Precision");
    (*reply)->elements[i++] = swarmkv_reply_new_integer(precision);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("TimeWindowMs");
    (*reply)->elements[i++] = swarmkv_reply_new_integer(time_window_ms);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("ErrorCMS");
    (*reply)->elements[i++] = swarmkv_reply_new_double(error_cms);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("ErrorHLL");
    (*reply)->elements[i++] = swarmkv_reply_new_double(error_hll);
    (*reply)->elements[i++] = swarmkv_reply_new_string_fmt("Probability");
    (*reply)->elements[i++] = swarmkv_reply_new_double(probability);
    assert(i == 14);

    return FINISHED;
}