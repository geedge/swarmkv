#pragma once

#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

enum swarmkv_data_type
{
	STRING,
	INTEGER,
	HASH,
	THROTTLE,
	__SWARMKV_DATA_TYPE_MAX
};
struct swarmkv_store;
struct swarmkv_store* swarmkv_store_new();
void swarmkv_store_free(struct swarmkv_store* store);


int swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);
int swarmkv_store_cache_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply);

int is_read_operator(enum swarmkv_operator op);


