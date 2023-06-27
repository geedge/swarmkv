#include "swarmkv_store.h"
#include "swarmkv_utils.h"
#include "swarmkv_monitor.h"
#include "swarmkv_sync.h"

#include "sds.h"
#include "mpack.h"
#include "uthash.h"
#include "utarray.h"
#include "utlist.h"
#include "xxhash.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>//timercmp

struct swarmkv_obj_specs
{
	enum sobj_type type;
	const char *type_name;
	void *(*obj_new) (uuid_t uuid);
	void (*obj_free) (void *obj);
	void (*obj_serialize) (const void *obj, char **blob, size_t *blob_sz);
	void (*obj_merge_blob) (void *obj, const char *blob, size_t blob_sz);
	size_t (*obj_size)(const void *obj);
};
static void *__wrap_OC_token_bucket_new(uuid_t uuid)
{
	struct OC_token_bucket *bucket=NULL;
	struct timeval beginning_of_history;
	memset(&beginning_of_history, 0, sizeof(beginning_of_history));
	bucket=OC_token_bucket_new(uuid, beginning_of_history, 0, 0);
	return bucket;
}
static void *__wrap_fair_token_bucket_new(uuid_t uuid)
{
	struct fair_token_bucket *bucket=NULL;
	struct timeval beginning_of_history;
	memset(&beginning_of_history, 0, sizeof(beginning_of_history));
	bucket=fair_token_bucket_new(uuid, beginning_of_history, 0, 0, 0);
	return bucket;
}
static void *__wrap_bulk_token_bucket_new(uuid_t uuid)
{
	struct bulk_token_bucket *bucket=NULL;
	struct timeval beginning_of_history;
	memset(&beginning_of_history, 0, sizeof(beginning_of_history));
	bucket=bulk_token_bucket_new(uuid, beginning_of_history, 0, 0, 0);
	return bucket;
}
size_t undefined_obj_mem_size(void *obj)
{
	return 0;
}
void undefined_obj_free(void *obj)
{
	assert(obj==NULL);
	return;
}
struct swarmkv_obj_specs sobj_specs[__SWARMKV_OBJ_TYPE_MAX] = 
{
	{
		.type=OBJ_TYPE_STRING,
		.type_name="string",
		.obj_new=(void * (*)(unsigned char *))LWW_register_new,
		.obj_free=(void (*)(void *))LWW_register_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))LWW_register_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))LWW_register_merge_blob,
		.obj_size=(size_t (*)(const void *))LWW_regeister_mem_size
	},
	{
		.type=OBJ_TYPE_INTEGER,
		.type_name="integer",
		.obj_new=(void * (*)(unsigned char *))PN_counter_new,
		.obj_free=(void (*)(void *))PN_counter_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))PN_counter_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))PN_counter_merge_blob,
		.obj_size=(size_t (*)(const void *))PN_counter_mem_size
	},
	{
		.type=OBJ_TYPE_SET,
		.type_name="set",
		.obj_new=(void * (*)(unsigned char *))OR_set_new,
		.obj_free=(void (*)(void *))OR_set_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))OR_set_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))OR_set_merge_blob,
		.obj_size=(size_t (*)(const void *))OR_set_mem_size
	},
	{
		.type=OBJ_TYPE_HASH,
		.type_name="hash",
		.obj_new=(void * (*)(unsigned char *))OR_map_new,
		.obj_free=(void (*)(void *))OR_map_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))OR_map_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))OR_map_merge_blob,
		.obj_size=(size_t (*)(const void *))OR_map_mem_size
	},
	{
		.type=OBJ_TYPE_TOKEN_BUCKET,
		.type_name="token-bucket",
		.obj_new=__wrap_OC_token_bucket_new,
		.obj_free=(void (*)(void *))OC_token_bucket_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))OC_token_bucket_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))OC_token_bucket_merge_blob,
		.obj_size=(size_t (*)(const void *))OC_token_bucket_mem_size,
	},
	{
		.type=OBJ_TYPE_FAIR_TOKEN_BUCKET,
		.type_name="fair-token-bucket",
		.obj_new=__wrap_fair_token_bucket_new,
		.obj_free=(void (*)(void *))fair_token_bucket_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))fair_token_bucket_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))fair_token_bucket_merge_blob,
		.obj_size=(size_t (*)(const void *))fair_token_bucket_mem_size,
	},
	{
		.type=OBJ_TYPE_BULK_TOKEN_BUCKET,
		.type_name="bulk-token-bucket",
		.obj_new=__wrap_bulk_token_bucket_new,
		.obj_free=(void (*)(void *))bulk_token_bucket_free,
		.obj_serialize=(void (*)(const void *, char **, size_t *))bulk_token_bucket_serialize,
		.obj_merge_blob=(void (*)(void *, const char *, size_t))bulk_token_bucket_merge_blob,
		.obj_size=(size_t (*)(const void *))bulk_token_bucket_mem_size
	},
	{
		.type=OBJ_TYPE_UNDEFINED,
		.type_name="undefined",
		.obj_new=NULL,
		.obj_free=undefined_obj_free,
		.obj_serialize=NULL,
		.obj_merge_blob=NULL,
		.obj_size=(size_t (*)(const void *))undefined_obj_mem_size
	}
};
#define	MODULE_SWAMRKV_STORE	module_name_str("swarmkv.store")
#define STORE_SHARD_NUMBER	8
struct swarmkv_store_thread
{
	struct scontainer *obj_table;
	struct scontainer *sync_queue;
	pthread_rwlock_t rwlock;
};
struct swarmkv_store
{
	struct swarmkv_module module;
	node_t self;
	size_t nr_worker_threads;
	struct swarmkv_store_thread *threads;
	struct scontainer *obj_table[STORE_SHARD_NUMBER];
	struct scontainer *sync_queue[STORE_SHARD_NUMBER];
//	pthread_mutex_t mutex[STORE_SHARD_NUMBER];
	pthread_rwlock_t rwlock[STORE_SHARD_NUMBER];
	exec_cmd_func *exec_cmd;
	struct swarmkv *exec_cmd_handle;
	uuid_t my_uuid;	
	struct swarmkv_module *mod_monitor;
	long long sync_err;
	long long sync_ok;
	long long synced;
};


UT_icd ut_node_addr_icd = {sizeof(node_t), NULL, NULL, NULL};

struct swarmkv_store *module2store(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "store"));
	struct swarmkv_store *store=container_of(module, struct swarmkv_store, module);
	assert(store==module->mod_ctx);
	return store;
}

int get_shard_id(sds key)
{
	int shard_idx=key_hash_slot(key, sdslen(key))%STORE_SHARD_NUMBER;
	return shard_idx;
}
int store_gettid(struct swarmkv_module *mod, sds key)
{
	struct swarmkv_store *store=module2store(mod);
	if(!key) return -1;
	int tid=key_hash_slot(key, sdslen(key))%store->nr_worker_threads;
	return tid;
}
void store_lock(struct swarmkv_module *mod, enum cmd_key_flag flag, sds key)
{
	struct swarmkv_store *store=module2store(mod);
	if(!key) return;
	int shard_idx=get_shard_id(key);
	if(flag==CMD_KEY_RO)
	{
		pthread_rwlock_rdlock(&(store->rwlock[shard_idx]));
	}
	else
	{
		pthread_rwlock_wrlock(&(store->rwlock[shard_idx]));
	}
	return;
}
void store_unlock(struct swarmkv_module *mod, enum cmd_key_flag flag, sds key)
{
	struct swarmkv_store *store=module2store(mod);
	if(!key) return;
	int shard_idx=get_shard_id(key);
	pthread_rwlock_unlock(&(store->rwlock[shard_idx]));

}
struct scontainer
{
	struct sobj obj;
	struct timeval op_timestamp;
	char is_pending;	//waiting for reply 0f CRDT PULL.
	char is_in_table;
	char is_in_sync_q;
	int shard_idx;
	UT_array *replica_node_list; //used by value owner for cache synchronization
	UT_hash_handle hh;
	struct scontainer *prev; //hook to sync queue
	struct scontainer *next; //hook to sync queue
};

static struct scontainer *scontainer_new(enum sobj_type type, const sds key)
{
	struct scontainer *ctr=ALLOC(struct scontainer, 1);
	ctr->obj.type=type;
	ctr->obj.key=sdsdup(key);	
	ctr->shard_idx=get_shard_id(key);
	gettimeofday(&ctr->op_timestamp, NULL);
	return ctr;
}

static void scontainer_free(struct scontainer *ctr)
{
	assert(!ctr->is_in_table);
	if(ctr->replica_node_list)
	{
		utarray_free(ctr->replica_node_list);
		ctr->replica_node_list=NULL;
	}
	assert(ctr->obj.type<__SWARMKV_OBJ_TYPE_MAX);
	sobj_specs[ctr->obj.type].obj_free(ctr->obj.raw);
	sdsfree(ctr->obj.key);
	free(ctr);
	return;
}

static struct scontainer *scontainer_find(struct scontainer **table, const sds key)
{
	struct scontainer *ctr=NULL;
	HASH_FIND(hh, *table, key, sdslen(key), ctr);
	return ctr;
}
static void scontainer_join(struct scontainer **table, struct scontainer *ctr)
{
	HASH_ADD_KEYPTR(hh, *table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
	ctr->is_in_table=1;
	return;	
}
static void scontainer_remove(struct scontainer **table, struct scontainer *ctr)
{
	HASH_DELETE(hh, *table, ctr);
	ctr->is_in_table=0;
	return;
}


static int node_cmp(const void *a, const void *b)
{
	return node_compare((node_t *) a, (node_t *) b);
}
static int scontainer_add_replica_node(struct scontainer *ctr, const node_t *node)
{
	node_t *found=NULL;
	if(!ctr->replica_node_list)
	{
		utarray_new(ctr->replica_node_list, &ut_node_addr_icd);
	}
	found=(node_t*)utarray_find(ctr->replica_node_list, node, node_cmp);
	if(found)
	{
		return 0;
	}
	utarray_push_back(ctr->replica_node_list, node);
	utarray_sort(ctr->replica_node_list, node_cmp);
	return 1;
}
static void scontainer_remove_replica_node(struct scontainer *ctr, const node_t *node)
{
	node_t *found=NULL;
	if(!ctr->replica_node_list) return;
	found=(node_t*)utarray_find(ctr->replica_node_list, node, node_cmp);
	if(!found) return;
	int pos=utarray_eltidx(ctr->replica_node_list, found);
	utarray_erase(ctr->replica_node_list, pos, 1);
	return;
}
void store_add_scontainer(struct swarmkv_store *store, struct scontainer *ctr)
{
	scontainer_join(&(store->obj_table[ctr->shard_idx]), ctr);
	return;	
}
void store_remove_scontainer(struct swarmkv_store *store, struct scontainer *ctr)
{
	HASH_DELETE(hh, store->obj_table[ctr->shard_idx], ctr);	
	ctr->is_in_table=0;
	if(ctr->is_in_sync_q)
	{
		DL_DELETE(store->sync_queue[ctr->shard_idx], ctr);
		ctr->is_in_sync_q=0;
	}
}
typedef void sobj_callback_func_t(struct sobj * obj, void *cb_arg);
void store_iterate_sobj(struct swarmkv_store *store, sobj_callback_func_t *cb, void *cb_arg)
{
    struct scontainer *ctr=NULL, *tmp=NULL;


    for(size_t i=0; i<STORE_SHARD_NUMBER; i++)
    {
		pthread_rwlock_rdlock(&(store->rwlock[i]));
        HASH_ITER(hh, store->obj_table[i], ctr, tmp)
        {
			if(!ctr->is_pending)
			{
				cb(&ctr->obj, cb_arg);
			}
		}
		pthread_rwlock_unlock(&(store->rwlock[i]));
    }

}
struct scontainer *store_lookup_scontainer(struct swarmkv_store *store, sds key)
{
	struct scontainer *ctr=NULL;
	int shard_idx=get_shard_id(key);
	ctr=scontainer_find(&(store->obj_table[shard_idx]), key);
	if(ctr)
	{
		return ctr;
	}
	else
	{
		return NULL;
	}	
}
struct sobj *store_lookup(struct swarmkv_module *mod_store, sds key)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL;
	ctr=store_lookup_scontainer(store, key);
	if(ctr) //&& !ctr->is_pending
	{
		return &(ctr->obj);
	}
	else
	{
		return NULL;
	}
}
void store_get_uuid(struct swarmkv_module* mod_store, uuid_t uuid)
{
	struct swarmkv_store *store=module2store(mod_store);
	uuid_copy(uuid, store->my_uuid);
	return;
}

void sobj_need_sync(struct swarmkv_module *mod_store, struct sobj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	gettimeofday(&ctr->op_timestamp, NULL);
	if(!ctr->is_in_sync_q && ctr->replica_node_list)
	{
		DL_APPEND(store->sync_queue[ctr->shard_idx], ctr);
		ctr->is_in_sync_q=1;
	}
	return;
}
int sobj_get_random_replica(struct sobj *obj, node_t *out)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	if(!ctr->replica_node_list) return 0;
	node_t *replica=(node_t*)utarray_eltptr(ctr->replica_node_list, 0);
	node_copy(out, replica);
	return 1;
}
void scontainer_serialize(struct scontainer *ctr, char **blob, size_t *blob_sz)
{
	char *value_blob=NULL;
	size_t value_blob_sz=0;
	struct sobj *obj=&ctr->obj;
	sobj_specs[obj->type].obj_serialize(obj->raw, &value_blob, &value_blob_sz);
	char *mpack_buff=NULL;
	size_t mpack_sz=0;	
	mpack_sz=sizeof(obj->type)+sizeof(ctr->op_timestamp);
	mpack_sz+=sizeof(size_t)+value_blob_sz;
	mpack_buff=ALLOC(char, mpack_sz);
	size_t offset=0;

	memcpy(mpack_buff, &obj->type, sizeof(obj->type));
	offset+=sizeof(obj->type);
	memcpy(mpack_buff+offset, &ctr->op_timestamp, sizeof(ctr->op_timestamp));
	offset+=sizeof(ctr->op_timestamp);

	memcpy(mpack_buff+offset, &value_blob_sz, sizeof(size_t));
	offset+=sizeof(size_t);

	memcpy(mpack_buff+offset, value_blob, value_blob_sz);
	offset+=value_blob_sz;
	assert(offset==mpack_sz);
	*blob=mpack_buff;
	*blob_sz=mpack_sz;

	free(value_blob);
	return;
}

void sobj_merge_blob(struct sobj *obj, const char *blob, size_t blob_sz, uuid_t uuid)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);

	struct timeval now;

	enum sobj_type type;
	size_t offset=0;
	memcpy(&type, blob+offset, sizeof(type));
	offset+=sizeof(type);
	assert(ctr->obj.type==type || ctr->obj.type==OBJ_TYPE_UNDEFINED);
	ctr->obj.type=type;

	memcpy(&ctr->op_timestamp, blob+offset, sizeof(ctr->op_timestamp));
	offset+=sizeof(ctr->op_timestamp);

	gettimeofday(&now, NULL);
	if(timercmp(&ctr->op_timestamp, &now, >))
	{
		fprintf(stderr, "Key %s is merging a blob which has a operation time in the future!\n", obj->key);
	}
	size_t value_blob_sz=0;
	memcpy(&value_blob_sz, blob+offset, sizeof(size_t));
	offset+=sizeof(size_t);
	assert(offset+value_blob_sz==blob_sz);
	const char *value_blob=blob+offset;
	if(!obj->raw)
	{
		obj->raw=sobj_specs[obj->type].obj_new(uuid);
	}
	sobj_specs[obj->type].obj_merge_blob(obj->raw, value_blob, value_blob_sz);
	return;

}

enum CRDT_OP
{
	CRDT_PULL,
	CRDT_PUSH,
	CRDT_JOIN
};
struct crdt_generic_ctx
{
	enum CRDT_OP op;
	struct swarmkv_store *store;
	node_t peer;
	sds key;
	struct future *f;
};
void crdt_generic_ctx_free(struct crdt_generic_ctx *ctx)
{		
	sdsfree(ctx->key);
	ctx->store=NULL;
	future_destroy(ctx->f);
	free(ctx);
}

static void crdt_generic_on_succ(void* result, void* user)
{
	struct crdt_generic_ctx *ctx=(struct crdt_generic_ctx *)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply*) result;
	struct scontainer *ctr=NULL;
	uuid_t uuid;
	struct swarmkv_module *mod_store=&(ctx->store->module);
	store_get_uuid(&(ctx->store->module), uuid);
	switch(ctx->op)
	{
		case CRDT_PULL:
			store_lock(mod_store, CMD_KEY_OW, ctx->key);
			ctr=store_lookup_scontainer(ctx->store, ctx->key);
			if(ctr && reply->type==SWARMKV_REPLY_STRING)
			{
				sobj_merge_blob(&ctr->obj, reply->str, reply->len, uuid);
				ctr->is_pending=0;
				atomic_inc(&ctx->store->sync_ok);
			}
			else
			{
				atomic_inc(&ctx->store->sync_err);
			}
			store_unlock(&ctx->store->module, CMD_KEY_OW, ctx->key);
			break;
		case CRDT_PUSH:
			if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer>0)
			{
				atomic_add(&ctx->store->sync_ok, reply->integer);
			}
			else
			{
				atomic_inc(&ctx->store->sync_err);
			}
			break;
		case CRDT_JOIN:
			if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer==1)
			{
				atomic_inc(&ctx->store->sync_ok);
			}
			else
			{
				atomic_inc(&ctx->store->sync_err);
			}
			break;
		default:
			atomic_inc(&ctx->store->sync_err);
			break;
	}

	crdt_generic_ctx_free(ctx);
	return;
}
static void store_remove_failed_peer(struct swarmkv_store *store, int shard, const node_t *peer)
{
	struct scontainer *ctr=NULL, *tmp=NULL;
	pthread_rwlock_wrlock(&store->rwlock[shard]);
	HASH_ITER(hh, store->obj_table[shard], ctr, tmp)
	{
		scontainer_remove_replica_node(ctr, peer);
	}
	pthread_rwlock_unlock(&store->rwlock[shard]);
}
static void crdt_generic_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct crdt_generic_ctx *ctx=(struct crdt_generic_ctx *)user;
	atomic_inc(&ctx->store->sync_err);
	if(err==FUTURE_ERROR_TIMEOUT)
	{
		int shard_idx=get_shard_id(ctx->key);
		store_remove_failed_peer(ctx->store, shard_idx, &ctx->peer);
	}
	crdt_generic_ctx_free(ctx);
	return;
}

void crdt_generic_call(struct swarmkv_store *store, enum CRDT_OP op, const struct swarmkv_cmd *cmd, const node_t *peer)
{
	const char *symbol[]={"crdt_pull", "crdt_push", "crdt_join"};
	struct crdt_generic_ctx *ctx=NULL;
	ctx=ALLOC(struct crdt_generic_ctx, 1);
	ctx->op=op;
	ctx->store=store;
	ctx->key=sdsdup(cmd->argv[2]);
	node_copy(&ctx->peer, peer);
	ctx->f=future_create(symbol[op], crdt_generic_on_succ, crdt_generic_on_fail, ctx);
	store->exec_cmd(store->exec_cmd_handle, NULL, peer, cmd, ctx->f);
	return;
}

void swarmkv_store_periodic(struct swarmkv_module * mod_store)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL, *tmp=NULL;
	struct timespec start, end;
	int has_key_synced=0;

	clock_gettime(CLOCK_MONOTONIC, &start);
	//Ease the lock contention among work threads
	int shard=random()%STORE_SHARD_NUMBER;
	for(int i=0; i<STORE_SHARD_NUMBER; i++)
	{
		shard=(shard+i)%STORE_SHARD_NUMBER;
		struct sync_master *sync_master=sync_master_new();
		pthread_rwlock_wrlock(&store->rwlock[shard]);
		DL_FOREACH_SAFE(store->sync_queue[shard], ctr, tmp)
		{
			char *blob=NULL;
			size_t blob_sz=0;
			scontainer_serialize(ctr, &blob, &blob_sz);
			sync_master_add_obj(sync_master, ctr->obj.key, blob, blob_sz,
							utarray_front(ctr->replica_node_list), utarray_len(ctr->replica_node_list));
			DL_DELETE(store->sync_queue[shard], ctr);
			ctr->is_in_sync_q=0;
			store->synced++;
			has_key_synced=1;
		}
		pthread_rwlock_unlock(&store->rwlock[shard]);
		node_t peer;
		struct swarmkv_cmd *cmd=NULL;
		int ret=1;
		while(1)
		{
			ret=sync_master_get_cmd(sync_master, &peer, &cmd);
			if(!ret) break;	
			crdt_generic_call(store, CRDT_PUSH, cmd, &peer);
			swarmkv_cmd_free(cmd);
			cmd=NULL;
		}
		sync_master_free(sync_master);
	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	if(has_key_synced)
	{
		swarmkv_monitor_record_event(store->mod_monitor, "crdt-sync-cycle", timespec_diff_usec(&start, &end));
	}
}
struct swarmkv_module *swarmkv_store_new(const struct swarmkv_options *opts, exec_cmd_func *send_cmd, void *handle_send_cmd)
{
	struct swarmkv_store *store=ALLOC(struct swarmkv_store, 1);
	strncpy(store->module.name, "store", sizeof(store->module.name));
	store->module.mod_ctx=store;
	store->nr_worker_threads=opts->nr_worker_threads;
	store->threads=ALLOC(struct swarmkv_store_thread, opts->nr_worker_threads);
	for(size_t i=0; i<opts->nr_worker_threads; i++)
	{
		pthread_rwlock_init(&(store->threads[i].rwlock), NULL);
		
	}
	uuid_copy(store->my_uuid, opts->bin_uuid);
	node_init(&store->self, opts->cluster_announce_ip, opts->cluster_announce_port);

	store->exec_cmd=send_cmd;
	store->exec_cmd_handle=handle_send_cmd;
	return &(store->module);
}

void swarmkv_store_free(struct swarmkv_module *mod_store)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL, *tmp=NULL;
	
	for(size_t i=0; i<store->nr_worker_threads; i++)
	{

		HASH_ITER(hh, store->threads[i].obj_table, ctr, tmp)
		{
			assert(ctr->shard_idx==i);
			scontainer_remove(&(store->threads[i].obj_table), ctr);
			scontainer_free(ctr);
		}		
		pthread_rwlock_destroy(&(store->threads[i].rwlock));
	}
	free(store);
	return;
}
void swarmkv_store_set_monitor_handle(struct swarmkv_module *mod_store, struct swarmkv_module *mod_monitor)
{
	struct swarmkv_store *store=module2store(mod_store);
	store->mod_monitor=mod_monitor;
}
void swarmkv_store_info(struct swarmkv_module *mod_store, struct store_info *info)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL;
	info->shards=STORE_SHARD_NUMBER;
	info->keys=0;
	info->keys_to_sync=0;
	for(size_t i=0; i<STORE_SHARD_NUMBER; i++)
	{
		pthread_rwlock_rdlock(&store->rwlock[i]);
		DL_COUNT(store->sync_queue[i], ctr, info->keys_to_sync);
		info->keys+=HASH_COUNT(store->obj_table[i]);
		pthread_rwlock_unlock(&store->rwlock[i]);
	}
	info->sync_ok=__sync_add_and_fetch(&store->sync_ok, 0);
	info->sync_err=__sync_add_and_fetch(&store->sync_err, 0);
	info->synced=store->synced;
}
void swarmkv_store_add_key(struct swarmkv_module *mod_store, const sds key, node_t *replica_nodes, size_t n_replica_node)
{
	struct scontainer *ctr=NULL;
	struct swarmkv_store *store=module2store(mod_store);
	size_t max_pull_node_num=4;
	struct swarmkv_cmd *crdt_pull_cmd=NULL;
	crdt_pull_cmd=swarmkv_cmd_new(3);
	crdt_pull_cmd->argv[0]=sdsnew("crdt");
	crdt_pull_cmd->argv[1]=sdsnew("pull");
	crdt_pull_cmd->argv[2]=sdsdup(key);

	store_lock(mod_store, CMD_KEY_OW, key);
	ctr=store_lookup_scontainer(store, key);
	if(!ctr)
	{
		ctr=scontainer_new(OBJ_TYPE_UNDEFINED, key);
		if(n_replica_node>0)//need crdt pull from replicas
		{
			ctr->is_pending=1;
		}
		else
		{
			ctr->is_pending=0;
		}
		store_add_scontainer(store, ctr);
	}
	for(size_t i=0; i<n_replica_node; i++)
	{
		scontainer_add_replica_node(ctr, replica_nodes+i);
	}
	store_unlock(mod_store, CMD_KEY_OW, key);

	for(size_t i=0; i<n_replica_node; i++)
	{
		if(i<max_pull_node_num)
		{
			crdt_generic_call(store, CRDT_PULL, crdt_pull_cmd, replica_nodes+i);
		}
		else
		{
			struct swarmkv_cmd *crdt_join_cmd=NULL;
			crdt_join_cmd=swarmkv_cmd_new(4);
			crdt_join_cmd->argv[0]=sdsnew("crdt");
			crdt_join_cmd->argv[1]=sdsnew("join");
			crdt_join_cmd->argv[2]=sdsdup(ctr->obj.key);
			crdt_join_cmd->argv[3]=node_addr2sds(replica_nodes+i);
			crdt_generic_call(store, CRDT_JOIN, crdt_join_cmd, replica_nodes+i);
			swarmkv_cmd_free(crdt_join_cmd);
		}
	}
	swarmkv_cmd_free(crdt_pull_cmd);
	crdt_pull_cmd=NULL;
	return;
}
UT_icd ut_array_matched_reply = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};

struct pattern_match_arg
{
	sds pattern;
    UT_array *matched_replies;
};
enum cmd_exec_result type_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*TYPE key*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[1];
	obj=store_lookup(mod_store, key);
	if(!obj)
	{
		return NEED_KEY_ROUTE;
	}
	*reply=swarmkv_reply_new_string_fmt(sobj_specs[obj->type].type_name);
	return FINISHED;
}
enum cmd_exec_result crdt_pull_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT PULL key */
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL;
	const sds key=cmd->argv[2];
	char *blob=NULL;
	size_t blob_sz=0;
		
	assert(accessing_node!=NULL);//should never invoked by local
	ctr=store_lookup_scontainer(store, key);
	if(ctr)
	{
		scontainer_add_replica_node(ctr, accessing_node);
		if(ctr->obj.type!=OBJ_TYPE_UNDEFINED)
		{
			scontainer_serialize(ctr, &blob, &blob_sz);
			*reply=swarmkv_reply_new_string(blob, blob_sz);
			free(blob);
		}
		else
		{
			*reply=swarmkv_reply_new_nil();
		}
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}
	return FINISHED;
	
}
enum cmd_exec_result crdt_push_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT PUSH key blob [key blob ...]*/
	struct sobj *obj=NULL;
	sds key=NULL, blob=NULL;	
	assert(accessing_node!=NULL);//should never invoked by local
	long long synced=0;
	uuid_t uuid;
	int __attribute__((__unused__))first_key_shard_idx=0;
	int __attribute__((__unused__))shard_idx=0;
	first_key_shard_idx=get_shard_id(cmd->argv[2]);

	store_get_uuid(mod_store, uuid);
	for(size_t i=0; i<cmd->argc-2; i+=2)
	{
		key=cmd->argv[2+i];
		blob=cmd->argv[2+i+1];
		obj=store_lookup(mod_store, key);
		if(obj)
		{
			sobj_merge_blob(obj, blob, sdslen(blob), uuid);
			synced++;
		}
		shard_idx=get_shard_id(key);
		assert(shard_idx==first_key_shard_idx);
	}
	*reply=swarmkv_reply_new_integer(synced);
	obj=store_lookup(mod_store, key);
	return FINISHED;
}

enum cmd_exec_result crdt_del_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
    /* CRDT DEL <key> */
	sds key=cmd->argv[2];
	struct scontainer *ctr=NULL;
	struct swarmkv_store *store=module2store(mod_store);
	ctr=store_lookup_scontainer(store, key);
	if(ctr)
	{
		store_remove_scontainer(store, ctr);
		scontainer_free(ctr);
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}	
	return FINISHED;
}
enum cmd_exec_result crdt_join_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
	/* CRDT JOIN <key> IP:port*/
	struct scontainer *ctr=NULL;
	const sds key=cmd->argv[2];
	assert(accessing_node!=NULL);//should never invoked by local

    node_t replica_node;
    node_init_from_sds(&replica_node, cmd->argv[3]);
	struct swarmkv_store *store=module2store(mod_store);
	ctr=store_lookup_scontainer(store, key);
	int added=0;
	if(ctr)
	{
		added=scontainer_add_replica_node(ctr, &replica_node);
		*reply=swarmkv_reply_new_integer(added);
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}
	return FINISHED;
}

static void pattern_match_function(struct sobj * obj, void *cb_arg)
{
	int is_matched=0;
	struct pattern_match_arg *arg=(struct pattern_match_arg*)cb_arg;
    struct swarmkv_reply *r=NULL;
	is_matched=stringmatchlen(arg->pattern, sdslen(arg->pattern), obj->key, sdslen(obj->key), 0);
	if(is_matched)
	{
		r=swarmkv_reply_new_string(obj->key, sdslen(obj->key));
		utarray_push_back(arg->matched_replies, &r);
	}
	return;
}


enum cmd_exec_result crdt_keys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
//CRDT KEYS IP Port pattern
    int i=0, n_matched=0;
	struct pattern_match_arg cb_arg;
	cb_arg.pattern=cmd->argv[2];
    utarray_new(cb_arg.matched_replies, &ut_array_matched_reply);
	struct swarmkv_store *store=module2store(mod_store);
    store_iterate_sobj(store, pattern_match_function, &cb_arg);
	n_matched=utarray_len(cb_arg.matched_replies);
	if(n_matched>0)
	{
		*reply=swarmkv_reply_new_array(n_matched);
		for(i=0; i<n_matched; i++)
		{
			(*reply)->elements[i]=*(struct swarmkv_reply **)utarray_eltptr(cb_arg.matched_replies, i);
		}
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}

	utarray_free(cb_arg.matched_replies);
	cb_arg.matched_replies=NULL;
	cb_arg.pattern=NULL;
	return FINISHED;
}

enum cmd_exec_result crdt_exists_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
//CRDT EXISTS key
	sds key=cmd->argv[2];
	struct sobj *obj=NULL;
	obj=store_lookup(mod_store, key);
	*reply=swarmkv_reply_new_integer(obj?1:0);
	return FINISHED;
}


enum cmd_exec_result crdt_rlist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT RLIST key*/
	const sds key=cmd->argv[2];
	struct scontainer *ctr=NULL;
	struct swarmkv_store *store=module2store(mod_store);
	ctr=store_lookup_scontainer(store, key);
	if(!ctr)
	{
		*reply=swarmkv_reply_new_nil();
		return FINISHED;
	}
	if(!ctr->replica_node_list)
	{
		*reply=swarmkv_reply_new_array(0);
		return FINISHED;
	}
	*reply=swarmkv_reply_new_array(utarray_len(ctr->replica_node_list));
	for(size_t i=0; i< (*reply)->n_element; i++)
	{
		(*reply)->elements[i]=swarmkv_reply_new_node(utarray_eltptr(ctr->replica_node_list, i), 0);
	}
	return FINISHED;
}
enum cmd_exec_result crdt_info_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*CRDT INFO key*/
	const sds key=cmd->argv[2];
	struct scontainer *ctr=NULL;
	struct swarmkv_store *store=module2store(mod_store);
	ctr=store_lookup_scontainer(store, key);
	if(!ctr)
	{
		*reply=swarmkv_reply_new_array(0);
		return FINISHED;
	}
	size_t sz=0;
	sz+=sobj_specs[ctr->obj.type].obj_size(ctr->obj.raw);
	sz+=sizeof(struct scontainer)+sdslen(ctr->obj.key);
	size_t n_replica=0;
	n_replica=ctr->replica_node_list?utarray_len(ctr->replica_node_list):0;
	sz+=n_replica*sizeof(node_t);
	int i=0;
	*reply=swarmkv_reply_new_array(8);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Type");
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt(sobj_specs[ctr->obj.type].type_name);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Size");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(sz);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("Replicas");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(n_replica);
	(*reply)->elements[i++]=swarmkv_reply_new_string_fmt("LastModified");
	(*reply)->elements[i++]=swarmkv_reply_new_integer(ctr->op_timestamp.tv_sec);
	return FINISHED;
}
