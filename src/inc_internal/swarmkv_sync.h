#pragma once
#include "swarmkv_common.h"
#include "sds.h"

struct sync_master;
struct sync_master *sync_master_new(void);
void sync_master_add_obj(struct sync_master *mgr, const sds key, char *blob, size_t blob_sz, const node_t *peers, size_t n_peer);
int sync_master_get_cmd(struct sync_master *mgr, node_t *peer, struct swarmkv_cmd **cmd);
void sync_master_free(struct sync_master *mgr);