#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

struct swarmkv_store;
struct swarmkv_store* swarmkv_store_new();
void swarmkv_store_free(struct swarmkv_store* store);


struct swarmkv_reply *swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, 
	 const struct swarmkv_node_addr* accessing_node, struct swarmkv_cmd **out_sync_cmd, struct swarmkv_node_addr **accessed_nodes, size_t *n_accessed_node);
int swarmkv_store_cache_read(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
void swarmkv_store_update_cache(struct swarmkv_store *store, const struct swarmkv_cmd *sync_cmd, struct swarmkv_reply **reply);
void swarmkv_store_process_sync_reply(struct swarmkv_store *store, sds key, const struct swarmkv_node_addr* peer, const struct swarmkv_reply* reply, struct swarmkv_cmd **full_sync_cmd);

int swarmkv_store_exists_key(struct swarmkv_store *store, const sds key);


