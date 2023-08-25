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

struct swarmkv_store_thread
{
	struct scontainer *obj_table;
	struct scontainer *sync_queue;
	pthread_mutex_t sanity_lock;
	long long keys_to_sync;
	long long n_keys;
	long long calls;
};
struct swarmkv_store
{
	struct swarmkv_module module;
	node_t self;
	struct swarmkv_store_thread *threads;

	struct swarmkv *exec_cmd_handle;
	struct swarmkv_module *mod_monitor;
	long long sync_err;
	long long sync_ok;
	long long synced;
	const struct swarmkv_options *opts;
};


UT_icd ut_node_addr_icd = {sizeof(node_t), NULL, NULL, NULL};

struct swarmkv_store *module2store(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "store"));
	struct swarmkv_store *store=container_of(module, struct swarmkv_store, module);
	assert(store==module->mod_ctx);
	return store;
}

int __store_gettid(sds key, int nr_worker_threads)
{
	int shard_idx=key_hash_slot(key, sdslen(key))%nr_worker_threads;
	return shard_idx;
}
int store_gettid(struct swarmkv_module * mod_store, sds key)
{
	return __store_gettid(key, module2store(mod_store)->opts->nr_worker_threads);
}
struct scontainer
{
	struct sobj obj;
	struct timeval op_timestamp;
	char is_pending;	//waiting for reply 0f CRDT PULL.
	char is_in_table;
	char is_in_sync_q;
	int tid;
	UT_array *replica_node_list; //used by value owner for cache synchronization
	UT_hash_handle hh;
	struct scontainer *prev; //hook to sync queue
	struct scontainer *next; //hook to sync queue
};

static struct scontainer *scontainer_new(enum sobj_type type, const sds key, int tid)
{
	struct scontainer *ctr=ALLOC(struct scontainer, 1);
	ctr->obj.type=type;
	ctr->obj.key=sdsdup(key);
	ctr->tid=tid;
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
	scontainer_join(&(store->threads[ctr->tid].obj_table), ctr);
	return;	
}
void store_remove_scontainer(struct swarmkv_store *store, struct scontainer *ctr)
{
	HASH_DELETE(hh, store->threads[ctr->tid].obj_table, ctr);	
	ctr->is_in_table=0;
	if(ctr->is_in_sync_q)
	{
		DL_DELETE(store->threads[ctr->tid].sync_queue, ctr);
		ctr->is_in_sync_q=0;
	}
}
typedef void sobj_callback_func_t(struct sobj * obj, void *cb_arg);
void store_iterate_sobj(struct swarmkv_store *store, int tid, sobj_callback_func_t *cb, void *cb_arg)
{
    struct scontainer *ctr=NULL, *tmp=NULL;
	HASH_ITER(hh, store->threads[tid].obj_table, ctr, tmp)
	{
		if(!ctr->is_pending)
		{
			cb(&ctr->obj, cb_arg);
		}
	}
}
struct scontainer *store_lookup_scontainer(struct swarmkv_store *store, sds key)
{
	struct scontainer *ctr=NULL;
	int designated_tid=key2tid(key, store->opts->nr_worker_threads);
	int real_tid=__gettid(store->exec_cmd_handle);
	assert(designated_tid==real_tid);
	ctr=scontainer_find(&(store->threads[designated_tid].obj_table), key);
	if(0==pthread_mutex_trylock(&store->threads[designated_tid].sanity_lock))
	{
		pthread_mutex_unlock(&store->threads[designated_tid].sanity_lock);
	}
	else
	{
		assert(0);
	}
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
	uuid_copy(uuid, store->opts->bin_uuid);
	return;
}
void store_get_node_addr(struct swarmkv_module* mod_store, node_t *node)
{
	struct swarmkv_store *store=module2store(mod_store);
	node_copy(node, &store->self);
	return;
}
void sobj_need_sync(struct swarmkv_module *mod_store, struct sobj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	gettimeofday(&ctr->op_timestamp, NULL);
	if(!ctr->is_in_sync_q && ctr->replica_node_list)
	{
		DL_APPEND(store->threads[ctr->tid].sync_queue, ctr);
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
enum cmd_exec_result handle_undefined_object(struct sobj *obj, struct swarmkv_reply **reply)
{
	assert(obj->type==OBJ_TYPE_UNDEFINED);
	node_t replica;
	int ret=0;
	ret=sobj_get_random_replica(obj, &replica);
	if(ret)
	{
		*reply=swarmkv_reply_new_node(&replica, 1);
		return REDIRECT;
	}
	return NEED_KEY_ROUTE;	
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

	//We can add replica node list to the blob, but the merge process will consume more CPU time.

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
	CRDT_GET,
	CRDT_MERGE,
	CRDT_MEET
};
struct crdt_generic_ctx
{
	enum CRDT_OP op;
	struct swarmkv_store *store;
	node_t peer;
	sds key;
};
void crdt_generic_ctx_free(struct crdt_generic_ctx *ctx)
{		
	sdsfree(ctx->key);
	ctx->store=NULL;
	free(ctx);
}
static void store_remove_failed_peer(struct swarmkv_store *store, int tid, const node_t *peer)
{
	struct scontainer *ctr=NULL, *tmp=NULL;
	if(tid>=store->opts->nr_worker_threads) return;//swarmkv_close() is called.
	HASH_ITER(hh, store->threads[tid].obj_table, ctr, tmp)
	{
		scontainer_remove_replica_node(ctr, peer);
	}
	return;
}
static void crdt_generic_on_reply(const struct swarmkv_reply *reply, void *user)
{
	struct crdt_generic_ctx *ctx=(struct crdt_generic_ctx *)user;
	struct scontainer *ctr=NULL;
	uuid_t uuid;
	store_get_uuid(&(ctx->store->module), uuid);
	

	 __attribute__ ((unused)) long long error_before=ctx->store->sync_err;
	if(reply->type==SWARMKV_REPLY_ERROR)
	{
		if(strcasestr(reply->str, "timed out"))
		{
			int tid=__gettid(ctx->store->exec_cmd_handle);
			atomic_inc(&ctx->store->sync_err);
			store_remove_failed_peer(ctx->store, tid, &ctx->peer);
		}
		goto error_out;
	}


	switch(ctx->op)
	{
		case CRDT_GET:
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
			break;
		case CRDT_MERGE:
			if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer>0)
			{
				atomic_add(&ctx->store->sync_ok, reply->integer);
			}
			else
			{
				atomic_inc(&ctx->store->sync_err);
			}
			break;
		case CRDT_MEET:
			if(reply->type==SWARMKV_REPLY_INTEGER)
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
	//assert(ctx->store->sync_err==error_before);
error_out:
	crdt_generic_ctx_free(ctx);
	return;
}

void crdt_generic_call(struct swarmkv_store *store, enum CRDT_OP op, const struct swarmkv_cmd *cmd, const node_t *peer)
{
	struct crdt_generic_ctx *ctx=NULL;
	assert(peer);
	ctx=ALLOC(struct crdt_generic_ctx, 1);
	ctx->op=op;
	ctx->store=store;
	ctx->key=sdsdup(cmd->argv[2]);
	node_copy(&ctx->peer, peer);
	swarmkv_async_command_on_argv(store->exec_cmd_handle, crdt_generic_on_reply, ctx, peer->addr, cmd->argc, cmd->argv);
	return;
}
#define MONITOR_SYNC_EVENT_NAME		"crdt-sync-cycle"
#define MAX_SYNC_PER_PERIOD	100000
int store_batch_sync(struct swarmkv_store *store, int tid)
{
	int n_synced=0;
	struct swarmkv_store_thread *thr=&store->threads[tid];
	struct sync_master *sync_master=sync_master_new();
	struct scontainer *ctr=NULL, *tmp=NULL;
	DL_FOREACH_SAFE(thr->sync_queue, ctr, tmp)
	{
		char *blob=NULL;
		size_t blob_sz=0;
		scontainer_serialize(ctr, &blob, &blob_sz);
		if(store->opts->batch_sync_enabled)
		{
			sync_master_add_obj(sync_master, ctr->obj.key, blob, blob_sz,
							utarray_front(ctr->replica_node_list), utarray_len(ctr->replica_node_list));

		}
		else
		{
			struct swarmkv_cmd *cmd=swarmkv_cmd_new(4);
			cmd->argv[0]=sdsnew("crdt");
			cmd->argv[1]=sdsnew("merge");
			cmd->argv[2]=sdsnew(ctr->obj.key);
			cmd->argv[3]=sdsnewlen(blob, blob_sz);
			for(int i=0; i<utarray_len(ctr->replica_node_list); i++)
			{
				node_t *peer=utarray_eltptr(ctr->replica_node_list, i);
				crdt_generic_call(store, CRDT_MERGE, cmd, peer);
			}
			swarmkv_cmd_free(cmd);
			free(blob);
		}
		DL_DELETE(thr->sync_queue, ctr);
		ctr->is_in_sync_q=0;
		store->synced++;
		n_synced++;
		if(n_synced>=MAX_SYNC_PER_PERIOD) break;
	}
	node_t peer;
	struct swarmkv_cmd *cmd=NULL;
	int ret=1;
	while(1)
	{
		ret=sync_master_get_cmd(sync_master, &peer, &cmd);
		if(!ret) break;	
		crdt_generic_call(store, CRDT_MERGE, cmd, &peer);
		swarmkv_cmd_free(cmd);
		cmd=NULL;
	}
	sync_master_free(sync_master);
	return n_synced;
}
void swarmkv_store_periodic(struct swarmkv_module * mod_store, int thread_id)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL, *tmp=NULL;
	struct timespec start, end;
	int n_synced=0;
	int real_tid=__gettid(store->exec_cmd_handle);
	assert(real_tid==thread_id);
	clock_gettime(CLOCK_MONOTONIC, &start);

	struct swarmkv_store_thread *thr=&store->threads[real_tid];
	thr->calls++;
	if(store->opts->batch_sync_enabled)
	{
		n_synced=store_batch_sync(store, real_tid);
	}
	else
	{
		DL_FOREACH_SAFE(thr->sync_queue, ctr, tmp)
		{
			char *blob=NULL;
			size_t blob_sz=0;
			scontainer_serialize(ctr, &blob, &blob_sz);

			struct swarmkv_cmd *cmd=swarmkv_cmd_new(4);
			cmd->argv[0]=sdsnew("crdt");
			cmd->argv[1]=sdsnew("merge");
			cmd->argv[2]=sdsnew(ctr->obj.key);
			cmd->argv[3]=sdsnewlen(blob, blob_sz);
			for(int i=0; i<utarray_len(ctr->replica_node_list); i++)
			{
				node_t *peer=utarray_eltptr(ctr->replica_node_list, i);
				crdt_generic_call(store, CRDT_MERGE, cmd, peer);
			}
			swarmkv_cmd_free(cmd);
			free(blob);
			
			DL_DELETE(thr->sync_queue, ctr);
			ctr->is_in_sync_q=0;
			store->synced++;
			n_synced++;
			if(n_synced>=MAX_SYNC_PER_PERIOD) break;
		}
	}
	thr->n_keys=HASH_COUNT(thr->obj_table);
	DL_COUNT(thr->sync_queue, ctr, thr->keys_to_sync);
	clock_gettime(CLOCK_MONOTONIC, &end);

	if(n_synced)
	{
		swarmkv_monitor_record_event(store->mod_monitor, MONITOR_SYNC_EVENT_NAME, timespec_diff_usec(&start, &end));
	}
}

struct swarmkv_module *swarmkv_store_new(const struct swarmkv_options *opts)
{
	struct swarmkv_store *store=ALLOC(struct swarmkv_store, 1);
	strncpy(store->module.name, "store", sizeof(store->module.name));
	store->module.mod_ctx=store;
	store->opts=opts;
	store->threads=ALLOC(struct swarmkv_store_thread, opts->nr_worker_threads);

	node_init(&store->self, opts->cluster_announce_ip, opts->cluster_announce_port);

	return &(store->module);
}
void swarmkv_store_set_exec_cmd_handle(struct swarmkv_module *mod_store, struct swarmkv *exec_cmd_handle)
{
	struct swarmkv_store *store=module2store(mod_store);
	store->exec_cmd_handle=exec_cmd_handle;
	return;
}
void swarmkv_store_free(struct swarmkv_module *mod_store)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL, *tmp=NULL;
	
	for(size_t i=0; i<store->opts->nr_worker_threads; i++)
	{

		HASH_ITER(hh, store->threads[i].obj_table, ctr, tmp)
		{
			assert(ctr->tid==i);
			scontainer_remove(&(store->threads[i].obj_table), ctr);
			scontainer_free(ctr);
		}
	}
	free(store->threads);
	store->threads=NULL;
	free(store);
	return;
}
void swarmkv_store_set_monitor_handle(struct swarmkv_module *mod_store, struct swarmkv_module *mod_monitor)
{
	struct swarmkv_store *store=module2store(mod_store);
	store->mod_monitor=mod_monitor;
	swarmkv_monitor_register_event(mod_monitor, MONITOR_SYNC_EVENT_NAME);
}
void swarmkv_store_info(struct swarmkv_module *mod_store, struct store_info *info)
{
	struct swarmkv_store *store=module2store(mod_store);
	info->keys=0;
	info->keys_to_sync=0;
	struct swarmkv_store_thread *thread=NULL;
	for(size_t i=0; i<store->opts->nr_worker_threads; i++)
	{
		thread = store->threads+i;
		info->keys_to_sync += __sync_add_and_fetch(&thread->keys_to_sync, 0);
		info->keys += __sync_add_and_fetch(&thread->n_keys, 0);
	}
	info->sync_ok=__sync_add_and_fetch(&store->sync_ok, 0);
	info->sync_err=__sync_add_and_fetch(&store->sync_err, 0);
	info->synced=store->synced;
}

UT_icd ut_array_matched_reply = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};

struct pattern_match_arg
{
	sds pattern;
    UT_array *matched_replies;
};
enum cmd_exec_result type_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
enum cmd_exec_result crdt_add_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*CRDT ADD key [IP:port ...]*/
	struct scontainer *ctr=NULL;
	struct swarmkv_store *store=module2store(mod_store);
	size_t max_pull_node_num=4;

	sds key=cmd->argv[2];
	int tid=store_gettid(mod_store, key);
	int real_tid=__gettid(store->exec_cmd_handle);
	assert(tid==real_tid);
	struct swarmkv_cmd *crdt_get_cmd=NULL;
	crdt_get_cmd=swarmkv_cmd_new(3);
	crdt_get_cmd->argv[0]=sdsnew("crdt");
	crdt_get_cmd->argv[1]=sdsnew("get");
	crdt_get_cmd->argv[2]=sdsdup(key);
	size_t n_replica_node=cmd->argc-3;

	ctr=store_lookup_scontainer(store, key);
	if(!ctr)
	{
		ctr=scontainer_new(OBJ_TYPE_UNDEFINED, key, tid);
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
	node_t *replica_nodes=ALLOC(node_t, n_replica_node);
	for(size_t i=0; i<n_replica_node; i++)
	{
		node_init_from_sds(&replica_nodes[i], cmd->argv[3+i]);
		assert(node_compare(replica_nodes+i, &store->self)!=0);
		scontainer_add_replica_node(ctr, replica_nodes+i);
	}
	struct swarmkv_cmd *crdt_meet_cmd=NULL;
	for(size_t i=0; i<n_replica_node; i++)
	{
		if(i<max_pull_node_num)
		{
			crdt_generic_call(store, CRDT_GET, crdt_get_cmd, replica_nodes+i);
		}
		crdt_meet_cmd=swarmkv_cmd_new(4);
		crdt_meet_cmd->argv[0]=sdsnew("crdt");
		crdt_meet_cmd->argv[1]=sdsnew("meet");
		crdt_meet_cmd->argv[2]=sdsdup(ctr->obj.key);
		crdt_meet_cmd->argv[3]=node_addr2sds(&store->self);
		crdt_generic_call(store, CRDT_MEET, crdt_meet_cmd, replica_nodes+i);
		swarmkv_cmd_free(crdt_meet_cmd);
	}
	swarmkv_cmd_free(crdt_get_cmd);
	crdt_get_cmd=NULL;
	free(replica_nodes);
	*reply=swarmkv_reply_new_status("OK");
	return FINISHED;
}
enum cmd_exec_result crdt_get_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* CRDT GET key */
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL;
	const sds key=cmd->argv[2];
	char *blob=NULL;
	size_t blob_sz=0;
	
	ctr=store_lookup_scontainer(store, key);
	if(ctr)
	{
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
enum cmd_exec_result crdt_merge_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* CRDT MERGE key blob [key blob ...]*/
	struct sobj *obj=NULL;
	sds key=NULL, blob=NULL;	
	long long synced=0;
	uuid_t uuid;
	int __attribute__((__unused__))first_key_tid=0;
	int __attribute__((__unused__))tid=0;
	first_key_tid=store_gettid(mod_store, cmd->argv[2]);

	node_t self;
	store_get_node_addr(mod_store, &self);
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
		tid=store_gettid(mod_store, key);
		assert(tid==first_key_tid);
	}
	*reply=swarmkv_reply_new_integer(synced);
	return FINISHED;
}

enum cmd_exec_result crdt_del_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
enum cmd_exec_result crdt_meet_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	/* CRDT MEET <key> IP:port [IP:port ...]*/
	struct scontainer *ctr=NULL;
	const sds key=cmd->argv[2];
	
    node_t replica_node;
   
	struct swarmkv_store *store=module2store(mod_store);
	//assert(node_compare(&replica_node, &store->self)!=0);
	//printf("CRDT MEET %s %s <- %s\n", key, store->self.addr, cmd->argv[3]);
	ctr=store_lookup_scontainer(store, key);
	int added=0;
	if(ctr)
	{
		for(int i=0; i<cmd->argc-3; i++)
		{
			node_init_from_sds(&replica_node, cmd->argv[3+i]);
			if(node_compare(&replica_node, &store->self)==0)
			{
				continue;
			}
			added+=scontainer_add_replica_node(ctr, &replica_node);
		}
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


enum cmd_exec_result crdt_keys_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
//CRDT KEYS tid pattern
    int i=0, n_matched=0;
	struct pattern_match_arg cb_arg;
	int thread_id=atoll(cmd->argv[2]);
	cb_arg.pattern=cmd->argv[3];
    utarray_new(cb_arg.matched_replies, &ut_array_matched_reply);
	struct swarmkv_store *store=module2store(mod_store);
	if(thread_id>store->opts->nr_worker_threads)
	{
		*reply=swarmkv_reply_new_error("Invalid thread id");
		return FINISHED;
	}
    store_iterate_sobj(store, thread_id, pattern_match_function, &cb_arg);
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

enum cmd_exec_result crdt_exists_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
//CRDT EXISTS key
	sds key=cmd->argv[2];
	struct sobj *obj=NULL;
	obj=store_lookup(mod_store, key);
	*reply=swarmkv_reply_new_integer((obj && obj->type!=OBJ_TYPE_UNDEFINED)?1:0);
	return FINISHED;
}


enum cmd_exec_result crdt_rlist_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
enum cmd_exec_result crdt_info_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
