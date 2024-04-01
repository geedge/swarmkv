#pragma once
#include "swarmkv_common.h"
#include "sds.h"

struct sync_master;
struct sync_task;
struct sync_master *sync_master_new(void);
void sync_master_add_obj(struct sync_master *mgr, const sds key, char *blob, size_t blob_sz, const node_t *peers, size_t n_peer);
struct sync_task *sync_master_get_task(struct sync_master *master);
void sync_task_free(struct sync_task *task);
const node_t *sync_task_peer(struct sync_task *task);
size_t sync_task_key_count(struct sync_task *task);
void sync_task_read_key_blob(struct sync_task *task, const char *argv[], size_t *argv_len, int argc);
void sync_master_free(struct sync_master *mgr);