#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"

#include <stdlib.h>
#include <assert.h>

enum cmd_exec_result get_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];
	obj = store_lookup(mod_store, key);
	const char *string_val = NULL;
	size_t string_sz = 0;
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED)
	{
		return handle_undefined_object(obj, reply);
	}
	if (obj->type == OBJ_TYPE_STRING)
	{
		LWW_register_get0(obj->string, &string_val, &string_sz);
		*reply = swarmkv_reply_new_string(string_val, string_sz);
	}
	else if (obj->type == OBJ_TYPE_INTEGER)
	{
		long long integer_val = PN_counter_get(obj->counter);
		*reply = swarmkv_reply_new_string_from_integer(integer_val);
	}
	else
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result set_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct sobj *obj = NULL;
	const sds key = cmd->argv[1];
	const sds value = cmd->argv[2];

	char *endptr = NULL;

	long long int_value = -1;
	int_value = strtol(cmd->argv[2], &endptr, 10);

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
		if (*endptr == '\0')
		{
			obj->type = OBJ_TYPE_INTEGER;
			obj->counter = PN_counter_new(uuid);
		}
		else
		{
			obj->type = OBJ_TYPE_STRING;
			obj->string = LWW_register_new(uuid);
		}
	}
	if (obj->type == OBJ_TYPE_STRING)
	{
		LWW_register_set(obj->string, value, sdslen(value));
		*reply = swarmkv_reply_new_status("OK");
		store_mark_object_as_modified(mod_store, obj);
	}
	else if (obj->type == OBJ_TYPE_INTEGER)
	{
		if (*endptr == '\0')
		{
			PN_counter_set(obj->counter, int_value);
			*reply = swarmkv_reply_new_status("OK");
			store_mark_object_as_modified(mod_store, obj);
		}
		else
		{
			*reply = swarmkv_reply_new_error(error_value_not_integer);
		}
	}
	else
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result integer_generic(struct swarmkv_module *mod_store, const sds key, long long increment, struct swarmkv_reply **reply)
{
	struct sobj *obj = NULL;
	long long value = 0;
	obj = store_lookup(mod_store, key);
	if (!obj)
	{
		return NEED_KEY_ROUTE;
	}
	if (obj->type == OBJ_TYPE_UNDEFINED || obj->type == OBJ_TYPE_INTEGER)
	{
		if (!obj->counter)
		{
			uuid_t uuid;
			store_get_uuid(mod_store, uuid);
			obj->counter = PN_counter_new(uuid);
			obj->type = OBJ_TYPE_INTEGER;
		}
		value = PN_counter_incrby(obj->counter, increment);
		*reply = swarmkv_reply_new_integer(value);
		store_mark_object_as_modified(mod_store, obj);
	}
	else
	{
		*reply = swarmkv_reply_new_error(error_wrong_type);
	}
	return FINISHED;
}
enum cmd_exec_result incrby_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	/* INCRBY key increment*/
	long long increment = 0;
	char *endptr = NULL;
	increment = strtol(cmd->argv[2], &endptr, 10);
	if (*endptr != '\0')
	{
		*reply = swarmkv_reply_new_error(error_value_not_integer);
		return FINISHED;
	}
	return integer_generic(mod_store, cmd->argv[1], increment, reply);
}
enum cmd_exec_result incr_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	/* INCR key*/
	return integer_generic(mod_store, cmd->argv[1], 1, reply);
}
enum cmd_exec_result decr_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	/* DECR key*/
	return integer_generic(mod_store, cmd->argv[1], -1, reply);
}
