#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "future_promise.h"


#include "lww_register.h"
#include "pn_counter.h"
#include "or_set.h"
#include "or_map.h"
#include "oc_token_bucket.h"
#include "fair_token_bucket.h"
#include "bulk_token_bucket.h"

enum sobj_type
{
	OBJ_TYPE_STRING,
	OBJ_TYPE_INTEGER,
	OBJ_TYPE_SET,
	OBJ_TYPE_HASH,
	OBJ_TYPE_TOKEN_BUCKET,
	OBJ_TYPE_FAIR_TOKEN_BUCKET,
	OBJ_TYPE_BULK_TOKEN_BUCKET,
	OBJ_TYPE_UNDEFINED,
	__SWARMKV_OBJ_TYPE_MAX
};

struct sobj
{
	enum sobj_type type;
	sds key;
	union
	{
		struct LWW_register *string;
		struct PN_counter *counter;
		struct OR_set *set;
		struct OR_map *hash;
		struct OC_token_bucket *bucket;
		struct fair_token_bucket *ftb;
		struct bulk_token_bucket *btb;
		void *raw;
	};
};

struct swarmkv_module *swarmkv_store_new(const struct swarmkv_options *opts, exec_cmd_func *send_cmd, void *handle_send_cmd);
void swarmkv_store_free(struct swarmkv_module* mod_store);
void swarmkv_store_periodic(struct swarmkv_module *module);
void swarmkv_store_set_monitor_handle(struct swarmkv_module *mod_store, struct swarmkv_module *mod_monitor);
void swarmkv_store_add_key(struct swarmkv_module *mod_store, const sds key, node_t *replica_nodes, size_t n_replica_node);
struct store_info
{
	long long keys;
	long long shards;
	long long keys_to_sync;
	long long sync_ok;
	long long sync_err;
	long long synced;
};
void swarmkv_store_info(struct swarmkv_module *module, struct store_info *info);

void sobj_need_sync(struct swarmkv_module *mod_store, struct sobj *obj);
int sobj_get_random_replica(struct sobj *obj, node_t *out);

void store_get_uuid(struct swarmkv_module* mod_store, uuid_t uuid);
struct sobj *store_lookup(struct swarmkv_module* mod_store, sds key);


enum cmd_exec_result crdt_pull_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_push_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_del_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_join_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_keys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_exists_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_rlist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result crdt_info_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);
enum cmd_exec_result type_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply);