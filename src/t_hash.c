#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "or_map.h"

#include <stdlib.h>
#include <assert.h>
static struct swarmkv_reply *or_map_kv_to_reply(const struct OR_map_kv *kv)
{
	struct swarmkv_reply *reply = NULL;
	switch (kv->type)
	{
	case TYPE_STRING:
		reply = swarmkv_reply_new_string(kv->string, kv->str_len);
		break;
	case TYPE_INTEGER:
		reply = swarmkv_reply_new_string_from_integer(kv->integer);
		break;
	default:
		reply = swarmkv_reply_new_nil();
		break;
	}
	return reply;
}
enum cmd_exec_result hset_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HSET key field value [field value ...]
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}

	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		uuid_t uuid;
		assert(obj->raw == NULL);
		store_get_uuid(mod_store, uuid);
		obj->hash = OR_map_new(uuid);
		obj->type = OBJ_TYPE_HASH;
	}
	size_t n_to_add = (cmd->argc - 2) / 2;
	size_t i = 0;
	int ret = 0, n_added = 0;

	sds field = NULL;
	sds value = NULL;
	long long int_val = 0;

	if (obj->type == OBJ_TYPE_HASH)
	{
		for (i = 0; i < n_to_add; i++)
		{
			field = cmd->argv[2 + i * 2];
			value = cmd->argv[2 + i * 2 + 1];
			if (is_number(value, sdslen(value), &int_val))
			{
				ret = OR_map_set_integer(obj->hash, field, sdslen(field), int_val);
			}
			else
			{
				ret = OR_map_set_string(obj->hash, field, sdslen(field),
										value, sdslen(value));
			}

			if (ret > 0)
				n_added++;
		}
		*reply = swarmkv_reply_new_integer(n_added);
		store_mark_object_as_modified(mod_store, obj);
	}
	else
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result hdel_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HDEL key field [field ...]
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type != OBJ_TYPE_HASH)
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	size_t n_to_del = cmd->argc - 2;
	size_t i = 0;
	int ret = 0, n_deleted = 0;

	sds field;
	for (i = 0; i < n_to_del; i++)
	{
		field = cmd->argv[2 + i];
		ret = OR_map_remove(obj->hash, field, sdslen(field));
		if (ret > 0)
			n_deleted++;
	}
	*reply = swarmkv_reply_new_integer(n_deleted);
	store_mark_object_as_modified(mod_store, obj);

	return FINISHED;
}
enum cmd_exec_result hmget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HMGET key field [field ...]
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type != OBJ_TYPE_HASH)
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	size_t n_to_get = cmd->argc - 2;
	struct OR_map_kv *kv_got = NULL;
	*reply = swarmkv_reply_new_array(n_to_get);
	for (size_t i = 0; i < n_to_get; i++)
	{
		kv_got = OR_map_get(obj->hash, cmd->argv[i + 2], sdslen(cmd->argv[i + 2]));
		if (!kv_got)
		{
			(*reply)->elements[i] = swarmkv_reply_new_nil();
		}
		else
		{
			(*reply)->elements[i] = or_map_kv_to_reply(kv_got);
			OR_map_kv_free(kv_got);
		}
	}
	return FINISHED;
}
enum cmd_exec_result hget_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	enum cmd_exec_result ret;
	struct swarmkv_reply *tmp_reply = NULL;
	ret = hmget_command(mod_store, cmd, &tmp_reply);
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

enum cmd_exec_result hgetall_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HGETALL key
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type != OBJ_TYPE_HASH)
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	struct OR_map_kv **kv_list = NULL;
	size_t n_kv = 0;
	n_kv = OR_map_get_all(obj->hash, &kv_list);
	*reply = swarmkv_reply_new_array(n_kv * 2);
	for (size_t i = 0; i < n_kv; i++)
	{
		(*reply)->elements[i * 2] = swarmkv_reply_new_string(kv_list[i]->key, kv_list[i]->key_len);
		(*reply)->elements[i * 2 + 1] = or_map_kv_to_reply(kv_list[i]);
		OR_map_kv_free(kv_list[i]);
		kv_list[i] = NULL;
	}
	free(kv_list);
	return FINISHED;
}
enum cmd_exec_result hincrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HINCRBY key field increment
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];
	sds field = cmd->argv[2];

	long long increment = 0;
	if (!is_number(cmd->argv[3], sdslen(cmd->argv[3]), &increment))
	{
		*reply = swarmkv_reply_new_error(error_value_not_integer);
		return FINISHED;
	}

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		uuid_t uuid;
		assert(obj->raw == NULL);
		store_get_uuid(mod_store, uuid);
		obj->hash = OR_map_new(uuid);
		obj->type = OBJ_TYPE_HASH;
	}
	int ret = 0;
	long long result = 0;

	if (obj->type == OBJ_TYPE_HASH)
	{
		ret = OR_map_incrby(obj->hash, field, sdslen(field), increment, &result);
		if (ret < 0)
		{
			*reply = swarmkv_reply_new_error(error_wrong_type);
		}
		else
		{
			*reply = swarmkv_reply_new_integer(result);
			store_mark_object_as_modified(mod_store, obj);
		}
	}
	else
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result hkeys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type != OBJ_TYPE_HASH)
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	struct OR_map_kv **kv_list = NULL;
	size_t n_kv = 0;
	n_kv = OR_map_keys(obj->hash, &kv_list);
	*reply = swarmkv_reply_new_array(n_kv);
	for (size_t i = 0; i < n_kv; i++)
	{
		(*reply)->elements[i] = swarmkv_reply_new_string(kv_list[i]->key, kv_list[i]->key_len);
		OR_map_kv_free(kv_list[i]);
		kv_list[i] = NULL;
	}
	free(kv_list);
	return FINISHED;
}
enum cmd_exec_result hlen_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	// HMGET key
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];

	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type != OBJ_TYPE_HASH)
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
		return FINISHED;
	}
	size_t n_field = 0;
	n_field = OR_map_cardinality(obj->hash);
	*reply = swarmkv_reply_new_integer((long long)n_field);
	return FINISHED;
}