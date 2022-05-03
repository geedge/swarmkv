#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "future_promise.h"

enum swarmkv_obj_type
{
	OBJ_TYPE_STRING,
	OBJ_TYPE_HASH,
	OBJ_TYPE_THROTTLE,
	__SWARMKV_OBJ_TYPE_MAX
};
struct obj_container;
struct swarmkv_obj
{
	enum swarmkv_obj_type type;
	sds key;
	union
	{
		sds val_string;
		struct obj_container* hash;		
	};
};


struct swarmkv_store;
typedef void send_cmd_func(struct swarmkv *handle, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer, struct future *f);
struct swarmkv_module *swarmkv_store_new(send_cmd_func *send_cmd, void *handle_send_cmd, int disable_cache);
void swarmkv_store_free(struct swarmkv_module* mod_store);
void swarmkv_store_periodic(struct swarmkv_module *module);



struct swarmkv_obj *swarmkv_obj_new(enum swarmkv_obj_type type, const sds key);
void swarmkv_obj_free(struct swarmkv_obj *obj);
int swarmkv_obj_get_ttl(struct swarmkv_obj *obj);

int store_ttl_key(struct swarmkv_module* mod_store, sds key);
int store_expire_key(struct swarmkv_module* mod_store, sds key, int seconds);


struct swarmkv_obj *store_lookup(struct swarmkv_module* mod_store, sds key);
void store_add_obj(struct swarmkv_module *mod_store, struct swarmkv_obj *obj);
void store_remove_obj(struct swarmkv_module *mod_store, struct swarmkv_obj *obj);
int store_obj_setnx_accessed_node(struct swarmkv_module* mod_store, struct swarmkv_obj *obj, const struct swarmkv_node_addr *node);

struct swarmkv_obj *store_cache_lookup(struct swarmkv_module* mod_store, sds key);

enum cache_sync_op
{
	CACHE_SET,
	CACHE_DEL
};
void store_cache_notify_all(struct swarmkv_module *mod_store, enum cache_sync_op op, struct swarmkv_obj *obj);
void store_cache_flush(struct swarmkv_module *mod_store, enum cache_sync_op op, const sds key, const sds blob);

