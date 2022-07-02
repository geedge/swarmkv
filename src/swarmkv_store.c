#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "mpack.h"
#include "uthash.h"
#include "utarray.h"
#include "utlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>//timercmp



#define	MODULE_SWAMRKV_STORE	module_name_str("swarmkv.store")

struct swarmkv_store
{
	struct swarmkv_module module;
	struct scontainer* obj_table;
	exec_cmd_func *exec_cmd;
	struct swarmkv *exec_cmd_handle;
	uuid_t my_uuid;	
	struct scontainer *sync_queue;
};


UT_icd ut_node_addr_icd = {sizeof(struct swarmkv_node_addr), NULL, NULL, NULL};

struct swarmkv_store *module2store(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "store"));
	struct swarmkv_store *store=container_of(module, struct swarmkv_store, module);
	assert(store==module->mod_ctx);
	return store;
}
struct scontainer
{
	struct sobj obj;
	struct timeval op_timestamp;
	char is_in_table;
	char is_in_sync_q;
	UT_array *replica_node_list; //used by value owner for cache synchronization
	UT_hash_handle hh;
	struct scontainer *prev; //hook to sync queue
	struct scontainer *next; //hook to sync queue
};


static void obj_container_free(struct scontainer *ctr)
{
	assert(!ctr->is_in_table);
	if(ctr->replica_node_list)
	{
		utarray_free(ctr->replica_node_list);
		ctr->replica_node_list=NULL;
	}
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			lww_register_free(ctr->obj.reg);
			ctr->obj.reg=NULL;
			break;
		case OBJ_TYPE_COUNTER:
			PN_counter_free(ctr->obj.counter);
			ctr->obj.counter=NULL;
			break;
		default:
			assert(0);
			break;
	}
	sdsfree(ctr->obj.key);
	free(ctr);
	return;
}

static struct scontainer *obj_container_find(struct scontainer **table, const sds key)
{
	struct scontainer *ctr=NULL;
	HASH_FIND(hh, *table, key, sdslen(key), ctr);
	return ctr;
}
static void obj_container_join(struct scontainer **table, struct scontainer *ctr)
{
	HASH_ADD_KEYPTR(hh, *table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
	ctr->is_in_table=1;
	return;	
}
static void obj_container_remove(struct scontainer **table, struct scontainer *ctr)
{
	HASH_DELETE(hh, *table, ctr);
	ctr->is_in_table=0;
	return;
}


static int node_cmp(const void *a, const void *b)
{
	return memcmp(a, b, sizeof(struct swarmkv_node_addr));
}
static int obj_container_add_replica_node(struct scontainer *ctr, const struct swarmkv_node_addr *node)
{
	struct swarmkv_node_addr* found=NULL;
	if(!ctr->replica_node_list)
	{
		utarray_new(ctr->replica_node_list, &ut_node_addr_icd);
	}
	found=(struct swarmkv_node_addr*)utarray_find(ctr->replica_node_list, node, node_cmp);
	if(found)
	{
		return 0;
	}
	utarray_push_back(ctr->replica_node_list, node);
	utarray_sort(ctr->replica_node_list, node_cmp);
	return 1;
}

static struct swarmkv_node_addr *obj_container_iterate_replica_nodes(struct scontainer *ctr, int *iterator)
{
	struct swarmkv_node_addr *addr=NULL;
	if(ctr->replica_node_list && *iterator < utarray_len(ctr->replica_node_list))
	{
		addr=utarray_eltptr(ctr->replica_node_list, *iterator);
		(*iterator)++;
	}
	return addr;
}

/*
static int obj_container_remove_accessed_node(struct scontainer *ctr, const struct swarmkv_node_addr *node)
{
	struct swarmkv_node_addr *found=NULL;
	size_t found_idx=0;
	if(!ctr->accessed_node_list)
	{
		utarray_new(ctr->accessed_node_list, &ut_node_addr_icd);
	}
	found=utarray_find(ctr->accessed_node_list, node, node_cmp);
	if (found)
	{
		found_idx=utarray_eltidx(ctr->accessed_node_list, found);
		utarray_erase(ctr->accessed_node_list, found_idx, 1);
		return 1;
	}
	return 0;
}
*/

struct sobj *sobj_new(enum sobj_type type, const sds key)
{
	struct scontainer *ctr=ALLOC(struct scontainer, 1);
	ctr->obj.type=type;
	ctr->obj.key=sdsdup(key);
	return &(ctr->obj);
}
void sobj_free(struct sobj *obj)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);

	assert(!ctr->is_in_table && !ctr->is_in_sync_q);
	if(ctr->replica_node_list)
	{
		utarray_free(ctr->replica_node_list);
		ctr->replica_node_list=NULL;
	}
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			lww_register_free(ctr->obj.reg);
			ctr->obj.reg=NULL;
			break;
		case OBJ_TYPE_COUNTER:
			PN_counter_free(ctr->obj.counter);
			ctr->obj.counter=NULL;
			break;
		default:
			assert(0);
			break;
	}
	sdsfree(ctr->obj.key);
	free(ctr);
	return;


	return;
}



void store_add_obj(struct swarmkv_module *mod_store, struct sobj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	obj_container_join(&(store->obj_table), ctr);

	return;	

}
void store_remove_obj(struct swarmkv_module *mod_store, struct sobj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	HASH_DELETE(hh, store->obj_table, ctr);	
	ctr->is_in_table=0;
	if(ctr->is_in_sync_q)
	{
		DL_DELETE(store->sync_queue, ctr);
		ctr->is_in_sync_q=0;
	}
	return;
}

struct sobj *store_lookup(struct swarmkv_module* mod_store, sds key)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct scontainer *ctr=NULL;
	ctr=obj_container_find(&(store->obj_table), key);
	if(ctr)
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
		DL_APPEND(store->sync_queue, ctr);
		ctr->is_in_sync_q=1;
	}
	return;
}
void sobj_serialize(struct sobj *obj, char **blob, size_t *blob_sz)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	char *value_blob=NULL;
	size_t value_blob_sz=0;
	mpack_writer_t snapshot_mpack;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;

	mpack_writer_init_growable(&snapshot_mpack, &mpack_buff, &mpack_sz);
	mpack_build_map(&snapshot_mpack);

	mpack_write_cstr(&snapshot_mpack, "t");
	mpack_write_int(&snapshot_mpack, obj->type);

	mpack_write_cstr(&snapshot_mpack, "k");
	mpack_write_str(&snapshot_mpack, obj->key, sdslen(obj->key));

	mpack_write_cstr(&snapshot_mpack, "ts");//timestamp
	mpack_write_str(&snapshot_mpack, (const char*)&(ctr->op_timestamp), sizeof(ctr->op_timestamp));

	mpack_write_cstr(&snapshot_mpack, "v");
	switch(obj->type)
	{
		case OBJ_TYPE_STRING:
			lww_register_serialize(obj->reg, &value_blob, &value_blob_sz);
			mpack_write_str(&snapshot_mpack, value_blob, value_blob_sz);
			free(value_blob);
			value_blob=NULL;
			break;
		case OBJ_TYPE_COUNTER:
			PN_counter_serialize(obj->counter, &value_blob, &value_blob_sz);
			mpack_write_str(&snapshot_mpack, value_blob, value_blob_sz);
			free(value_blob);
			value_blob=NULL;
			break;
		default:
			break;
	}

	mpack_complete_map(&snapshot_mpack);
	mpack_writer_destroy(&snapshot_mpack);
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
	return;
}

void sobj_merge_blob(struct swarmkv_module *mod_store, struct sobj *obj, const char *blob, size_t blob_sz)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);

	struct timeval op_timestamp, now;
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t item, v_node;
	enum sobj_type type=mpack_node_int(mpack_node_map_cstr(root, "t"));
	assert(ctr->obj.type==type || ctr->obj.type==OBJ_TYPE_UNDEFINED);
	ctr->obj.type=type;
	item=mpack_node_map_cstr(root, "k");
	assert(sdslen(ctr->obj.key)==mpack_node_strlen(item));
	assert(0==memcmp(ctr->obj.key, mpack_node_str(item), sdslen(ctr->obj.key)));
	item=mpack_node_map_cstr(root, "ts");
	assert(mpack_node_strlen(item)==sizeof(op_timestamp));
	memcpy(&op_timestamp, mpack_node_str(item), sizeof(op_timestamp));
	gettimeofday(&now, NULL);
	if(timercmp(&op_timestamp, &now, >))
	{
		fprintf(stderr, "Key %s is merging a blob which has a operation time in the future!\n", obj->key);
	}
	uuid_t uuid;	
	v_node=mpack_node_map_cstr(root, "v");
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			if(!obj->reg)
			{				
				store_get_uuid(mod_store, uuid);
				obj->reg=lww_register_new(NULL, 0);
			}
			lww_register_merge(obj->reg, mpack_node_str(v_node), mpack_node_strlen(v_node));
			break;
		case OBJ_TYPE_COUNTER:
			if(!obj->counter)
			{
				obj->counter=PN_counter_new((const char*)uuid, sizeof(uuid));
			}
			PN_counter_merge(obj->counter, mpack_node_str(v_node), mpack_node_strlen(v_node));
			break;
		default:
			assert(0);
			break;
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred while deserializing a sobj blob!\n");
	}
	return;

}
int sobj_add_replica_node(struct sobj *obj, const struct swarmkv_node_addr *node)
{
	struct scontainer *ctr=container_of(obj, struct scontainer, obj);
	int added=0;

	added=obj_container_add_replica_node(ctr, node);
	if(added)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

struct crdt_push_ctx
{
	struct future *f_crdt_push;
};
void crdt_push_ctx_free(struct crdt_push_ctx *ctx)
{
	future_destroy(ctx->f_crdt_push);
	free(ctx);
}
static void crdt_push_on_succ(void* result, void* user)
{
	struct crdt_push_ctx *ctx=(struct crdt_push_ctx *)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply*) result;
	assert(reply->type==SWARMKV_REPLY_INTEGER);
	if(reply->type==SWARMKV_REPLY_INTEGER)
	{
		assert(reply->integer==1);
	}
	crdt_push_ctx_free(ctx);
	return;

}
static void crdt_push_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct crdt_push_ctx *ctx=(struct crdt_push_ctx *)user;
	crdt_push_ctx_free(ctx);
	return;
}
void crdt_push(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer)
{
	struct crdt_push_ctx* ctx=NULL;
	ctx=ALLOC(struct crdt_push_ctx, 1);
	ctx->f_crdt_push=future_create("crdt_push", crdt_push_on_succ, crdt_push_on_fail, ctx);
	store->exec_cmd(store->exec_cmd_handle, cmd, peer, ctx->f_crdt_push);
	return;
}


static void scontainer_sync(struct swarmkv_store *store, struct scontainer *ctr)
{
	struct sobj *obj=&ctr->obj;
	struct swarmkv_cmd *crdt_push_cmd=NULL;
	char *blob=NULL;
	size_t blob_sz=0;

	sobj_serialize(obj, &blob, &blob_sz);
	crdt_push_cmd=swarmkv_cmd_new(4);
	crdt_push_cmd->argv[0]=sdsnew("crdt");
	crdt_push_cmd->argv[1]=sdsnew("push");
	crdt_push_cmd->argv[2]=sdsdup(obj->key);
	crdt_push_cmd->argv[3]=sdsnewlen(blob, blob_sz);
	free(blob);

	int iterator=0;
	struct swarmkv_node_addr *replica_node=NULL;
	while((replica_node=obj_container_iterate_replica_nodes(ctr, &iterator)) && replica_node)
	{
		crdt_push(store, crdt_push_cmd, replica_node);
	}
	swarmkv_cmd_free(crdt_push_cmd);
}
void swarmkv_store_periodic(struct swarmkv_module * module)
{
	struct swarmkv_store *store=module2store(module);
	struct scontainer *ctr=NULL, *tmp=NULL;

	pthread_rwlock_rdlock(&module->rwlock);
	DL_FOREACH_SAFE(store->sync_queue, ctr, tmp)
	{
		scontainer_sync(store, ctr);
		DL_DELETE(store->sync_queue, ctr);
		ctr->is_in_sync_q=0;
	}
	pthread_rwlock_unlock(&module->rwlock);
}
struct swarmkv_module *swarmkv_store_new(exec_cmd_func *send_cmd, void *handle_send_cmd, uuid_t node_uuid)
{
	struct swarmkv_store* store=ALLOC(struct swarmkv_store, 1);
	strncpy(store->module.name, "store", sizeof(store->module.name));
	store->module.mod_ctx=store;
	pthread_rwlock_init(&(store->module.rwlock), NULL);
	uuid_copy(store->my_uuid, node_uuid);
	store->exec_cmd=send_cmd;
	store->exec_cmd_handle=handle_send_cmd;
	return &(store->module);
}

void swarmkv_store_free(struct swarmkv_module *module)
{
	struct swarmkv_store *store=module2store(module);
	struct scontainer *ctr=NULL, *tmp=NULL;
	pthread_rwlock_destroy(&(store->module.rwlock));
	
	HASH_ITER(hh, store->obj_table, ctr, tmp)
	{
		obj_container_remove(&(store->obj_table), ctr);
		obj_container_free(ctr);
	}

	free(store);
	return;
}

struct crdt_pull_ctx
{
	struct swarmkv *db;
	struct swarmkv_module *mod_store;
	sds key;
	struct future *f_crdt_pull;
};
void crdt_pull_ctx_free(struct crdt_pull_ctx *ctx)
{
	sdsfree(ctx->key);
	ctx->key=NULL;
	ctx->mod_store=NULL;
	future_destroy(ctx->f_crdt_pull);
	ctx->f_crdt_pull=NULL;
	free(ctx);
}
static void crdt_pull_on_succ(void* result, void* user)
{
	struct crdt_pull_ctx *ctx=(struct crdt_pull_ctx *)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply*)result;
	struct sobj *obj=store_lookup(ctx->mod_store, ctx->key);
	if(obj)
	{
		sobj_merge_blob(ctx->mod_store, obj, reply->str, reply->len);
	}
	crdt_pull_ctx_free(ctx);
	return;
}
static void crdt_pull_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct crdt_pull_ctx *ctx=(struct crdt_pull_ctx *)user;
	crdt_pull_ctx_free(ctx);
}
void crdt_pull(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer)
{	
	struct swarmkv_store *store=module2store(mod_store);
	struct crdt_pull_ctx* ctx=NULL;
	ctx=ALLOC(struct crdt_pull_ctx, 1);
	ctx->key=sdsdup(cmd->argv[2]);
	ctx->mod_store=mod_store;
	ctx->f_crdt_pull=future_create("crdt_pull", crdt_pull_on_succ, crdt_pull_on_fail, ctx);
	store->exec_cmd(store->exec_cmd_handle, cmd, peer, ctx->f_crdt_pull);
	return;
}

struct sobj * swarmkv_store_add_key(struct swarmkv_module *mod_store, const sds key, struct swarmkv_node_addr *replica_nodes, size_t n_replica_node)
{
	struct sobj *obj=sobj_new(OBJ_TYPE_UNDEFINED, key);

	size_t max_pull_node_num=4;
	struct swarmkv_cmd *crdt_pull_cmd=NULL;
	crdt_pull_cmd=swarmkv_cmd_new(3);
	crdt_pull_cmd->argv[0]=sdsnew("crdt");
	crdt_pull_cmd->argv[1]=sdsnew("pull");
	crdt_pull_cmd->argv[2]=sdsdup(obj->key);

	store_add_obj(mod_store, obj);	
	
	for(size_t i=0; i<n_replica_node; i++)
	{
		sobj_add_replica_node(obj, replica_nodes+i);
		if(i<max_pull_node_num)
		{
			crdt_pull(mod_store, crdt_pull_cmd, replica_nodes+i);
		}
	}
	swarmkv_cmd_free(crdt_pull_cmd);
	crdt_pull_cmd=NULL;
	return obj;
}

