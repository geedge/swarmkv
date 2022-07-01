#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "future_promise.h"

#include "lww_register.h"
#include "pn_counter.h"

enum sobj_type
{
	OBJ_TYPE_STRING,
	OBJ_TYPE_COUNTER,
	OBJ_TYPE_HASH,
	OBJ_TYPE_THROTTLE,
	OBJ_TYPE_UNDEFINED,
	__SWARMKV_OBJ_TYPE_MAX
};
struct scontainer;
struct sobj
{
	enum sobj_type type;
	sds key;
	union
	{
		struct lww_register *reg;
		struct PN_counter *counter;
	};
};

struct swarmkv_module *swarmkv_store_new(exec_cmd_func *send_cmd, void *handle_send_cmd, uuid_t node_uuid);
void swarmkv_store_free(struct swarmkv_module* mod_store);
void swarmkv_store_periodic(struct swarmkv_module *module);
void swarmkv_store_add_key(struct swarmkv_module *mod_store, const sds key, struct swarmkv_node_addr *replica_nodes, size_t n_replica_node);



struct sobj *sobj_new(enum sobj_type type, const sds key);
void sobj_free(struct sobj *obj);
void sobj_need_sync(struct swarmkv_module *mod_store, struct sobj *obj);
void sobj_serialize(struct sobj *obj, char **blob, size_t *blob_sz);
void sobj_merge_blob(struct swarmkv_module *mod_store, struct sobj *obj, const char *blob, size_t blob_sz);
int sobj_add_replica_node(struct sobj *obj, const struct swarmkv_node_addr *node);

void store_get_uuid(struct swarmkv_module* mod_store, uuid_t uuid);
struct sobj *store_lookup(struct swarmkv_module* mod_store, sds key);
void store_add_obj(struct swarmkv_module *mod_store, struct sobj *obj);
void store_remove_obj(struct swarmkv_module *mod_store, struct sobj *obj);



