#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

struct swarmkv_store;
struct swarmkv_store* swarmkv_store_new();
void swarmkv_store_free(struct swarmkv_store* store);


int swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply, 
	 const struct swarmkv_node* accessing_node, struct swarmkv_cmd **sync_cmd, struct swarmkv_node **accessed_nodes, size_t *n_accessed_node);
int swarmkv_store_cache_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
void swarmkv_store_cache_sync(struct swarmkv_store *store, const struct swarmkv_cmd *sync_cmd, struct swarmkv_reply **reply);
void swarmkv_store_process_sync_reply(struct swarmkv_store *store, sds key, const struct swarmkv_node* peer, const struct swarmkv_reply* reply, struct swarmkv_cmd **full_sync_cmd);

int is_store_read_operator(enum swarmkv_operator op);
int swarmkv_store_remove_accessed_node(struct swarmkv_store *store, sds key, const struct swarmkv_node *node);


