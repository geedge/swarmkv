#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "mpack.h"
#include "uthash.h"
#include "utarray.h"
#include "timeout.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>



#define	MODULE_SWAMRKV_STORE	module_name_str("swarmkv.store")

struct swarmkv_store
{
	struct swarmkv_module module;
	struct obj_container* owned_obj_table;
	struct obj_container* cached_obj_table;
	send_cmd_func *send_cmd;
	struct swarmkv *handle_send_cmd;
	int disable_cache;
	struct timeouts *expires;
};


UT_icd ut_swarmkv_node_icd = {sizeof(struct swarmkv_node_addr), NULL, NULL, NULL};

struct swarmkv_store *module2store(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "store"));
	struct swarmkv_store *store=container_of(module, struct swarmkv_store, module);
	assert(store==module->mod_ctx);
	return store;
}
struct obj_container
{
	struct swarmkv_obj obj;
	char is_in_table;
	char is_expiring;
	uint64_t abs_timeout;
	UT_array *accessed_node_list; //used by value owner for cache synchronization
	UT_hash_handle hh;
	struct timeout timeout_handle;
};
static struct obj_container *obj_container_new(enum swarmkv_obj_type type, const sds key)
{
	struct obj_container *ctr=ALLOC(struct obj_container, 1);
	ctr->obj.type=type;
	ctr->obj.key=sdsdup(key);
	return ctr;
}

static void obj_container_free(struct obj_container *ctr)
{
	assert(!ctr->is_in_table);
	struct obj_container *field=NULL, *tmp_field=NULL;
	if(ctr->accessed_node_list)
	{
		utarray_free(ctr->accessed_node_list);
		ctr->accessed_node_list=NULL;
	}
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			sdsfree(ctr->obj.val_string);
			ctr->obj.val_string=NULL;
			break;
		case OBJ_TYPE_HASH:
			HASH_ITER(hh, ctr->obj.hash, field, tmp_field)
			{
				HASH_DELETE(hh, ctr->obj.hash, field);
				obj_container_free(field);
			}
			assert(ctr->obj.hash==NULL);
			break;
		default:
			assert(0);
			break;
	}
	sdsfree(ctr->obj.key);
	free(ctr);
	return;
}

static struct obj_container *obj_container_find(struct obj_container **table, const sds key)
{
	struct obj_container *ctr=NULL;
	HASH_FIND(hh, *table, key, sdslen(key), ctr);
	return ctr;
}
static void obj_container_join(struct obj_container **table, struct obj_container *ctr)
{
	HASH_ADD_KEYPTR(hh, *table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
	ctr->is_in_table=1;
	return;	
}
static void obj_container_remove(struct obj_container **table, struct obj_container *ctr)
{
	HASH_DELETE(hh, *table, ctr);
	ctr->is_in_table=0;
	return;
}

static sds obj_container_serialize(const struct obj_container *ctr)
{
	sds blob=NULL;
	mpack_writer_t snapshot_mpack;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	const struct swarmkv_obj *obj = &(ctr->obj);
	struct obj_container *field=NULL, *tmp_field=NULL;

	mpack_writer_init_growable(&snapshot_mpack, &mpack_buff, &mpack_sz);
	mpack_build_map(&snapshot_mpack);

	mpack_write_cstr(&snapshot_mpack, "t");
	mpack_write_int(&snapshot_mpack, obj->type);

	mpack_write_cstr(&snapshot_mpack, "to");
	mpack_write_u64(&snapshot_mpack, ctr->abs_timeout);

	mpack_write_cstr(&snapshot_mpack, "k");
	mpack_write_str(&snapshot_mpack, obj->key, sdslen(obj->key));

	mpack_write_cstr(&snapshot_mpack, "v");
	switch(obj->type)
	{
		case OBJ_TYPE_STRING:
			mpack_write_str(&snapshot_mpack, obj->val_string, sdslen(obj->val_string));
			break;
		case OBJ_TYPE_HASH:
			mpack_build_array(&snapshot_mpack);
			HASH_ITER(hh, obj->hash, field, tmp_field)
			{
				mpack_write_str(&snapshot_mpack, field->obj.key, sdslen(field->obj.key));
				mpack_write_str(&snapshot_mpack, field->obj.val_string, sdslen(field->obj.val_string));
			}
			mpack_complete_array(&snapshot_mpack);			
			break;
		default:
			break;
	}
	mpack_complete_map(&snapshot_mpack);
	mpack_writer_destroy(&snapshot_mpack);
	blob=sdsnewlen(mpack_buff, mpack_sz);
	free(mpack_buff);
	return blob;
}
static struct obj_container *obj_container_deserialize(const sds blob)
{
	struct obj_container *ctr=ALLOC(struct obj_container, 1);
	struct obj_container *field=NULL;
	size_t n_field=0, i=0;
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, sdslen(blob));
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t item, v_node;
	ctr->obj.type=mpack_node_int(mpack_node_map_cstr(root, "t"));
	item=mpack_node_map_cstr(root, "k");
	ctr->obj.key=sdsnewlen(mpack_node_str(item), mpack_node_strlen(item));
	item=mpack_node_map_cstr(root, "to");
	ctr->abs_timeout=mpack_node_u64(item);


	
	v_node=mpack_node_map_cstr(root, "v");
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			ctr->obj.val_string=sdsnewlen(mpack_node_str(v_node), mpack_node_strlen(v_node));
			break;
		case OBJ_TYPE_HASH:
			n_field=mpack_node_array_length(v_node);
			assert(n_field%2==0);
			n_field/=2;
			for(i=0; i<n_field; i++)
			{
				field=ALLOC(struct obj_container, 1);
				field->obj.type=OBJ_TYPE_STRING;
				item=mpack_node_array_at(v_node, 2*i);
				field->obj.key=sdsnewlen(mpack_node_str(item), mpack_node_strlen(item));
				item=mpack_node_array_at(v_node, 2*i+1);
				field->obj.val_string=sdsnewlen(mpack_node_str(item), mpack_node_strlen(item));
				obj_container_join(&(ctr->obj.hash), field);
			}
			break;
		default:
			assert(0);
			break;
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) {
		fprintf(stderr, "An error occurred deserializing the obj container!\n");
		obj_container_free(ctr);
		return NULL;
	}

	return ctr;
}
static int node_cmp(const void *a, const void *b)
{
	return memcmp(a, b, sizeof(struct swarmkv_node_addr));
}
static int obj_container_setnx_accessed_node(struct obj_container *ctr, const struct swarmkv_node_addr *node)
{
	struct swarmkv_node_addr* found=NULL;
	if(!ctr->accessed_node_list)
	{
		utarray_new(ctr->accessed_node_list, &ut_swarmkv_node_icd);
	}
	found=(struct swarmkv_node_addr*)utarray_find(ctr->accessed_node_list, node, node_cmp);
	if(found)
	{
		return 0;
	}
	utarray_push_back(ctr->accessed_node_list, node);
	utarray_sort(ctr->accessed_node_list, node_cmp);
	return 1;
}

static struct swarmkv_node_addr *obj_container_iterate_accessed_nodes(struct obj_container *ctr, int *iterator)
{
	struct swarmkv_node_addr *addr=NULL;
	if(ctr->accessed_node_list && *iterator < utarray_len(ctr->accessed_node_list))
	{
		addr=utarray_eltptr(ctr->accessed_node_list, *iterator);
		(*iterator)++;
	}
	return addr;
}

/*
static int obj_container_remove_accessed_node(struct obj_container *ctr, const struct swarmkv_node_addr *node)
{
	struct swarmkv_node_addr *found=NULL;
	size_t found_idx=0;
	if(!ctr->accessed_node_list)
	{
		utarray_new(ctr->accessed_node_list, &ut_swarmkv_node_icd);
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

struct cache_notify_ctx
{
	sds key;
	struct swarmkv_node_addr peer;
	struct future *f_cache_notify;
	struct swarmkv_store *store;
};
void cache_sync_ctx_free(struct cache_notify_ctx* ctx)
{
	future_destroy(ctx->f_cache_notify);
	ctx->f_cache_notify=NULL;
	sdsfree(ctx->key);
	ctx->key=NULL;
	free(ctx);
	return;
}

static void cache_notify_on_succ(void* result, void* user)
{
	struct cache_notify_ctx *ctx=(struct cache_notify_ctx *)user;
	const struct swarmkv_reply* reply=(const struct swarmkv_reply*) result;
	if(reply->type==SWARMKV_REPLY_STATUS)
	{ 
		assert(0==strncasecmp(reply->str, "OK", 2));
	}
	else if(reply->type==SWARMKV_REPLY_INTEGER)
	{
		assert(reply->integer==1);
	}
	else
	{
		assert(0);
	}
	cache_sync_ctx_free(ctx);
	return;

}
static void cache_notify_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cache_notify_ctx *ctx=(struct cache_notify_ctx *)user;
	cache_sync_ctx_free(ctx);
	return;
}
void store_cache_notify_one(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer)
{
	struct cache_notify_ctx* ctx=NULL;
	ctx=ALLOC(struct cache_notify_ctx, 1);	
	ctx->store=store;
	swarmkv_node_addr_copy(&(ctx->peer), peer);
	ctx->key=sdsdup(cmd->argv[1]);
	ctx->f_cache_notify=future_create("cache_sync", cache_notify_on_succ, cache_notify_on_fail, ctx);
	store->send_cmd(store->handle_send_cmd, cmd, peer, ctx->f_cache_notify);
	return;
}
void store_cache_notify_all(struct swarmkv_module *mod_store, enum cache_sync_op op, struct swarmkv_obj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);

	struct swarmkv_cmd *sync_cmd=NULL;
	if(op==CACHE_SET)
	{
		sync_cmd=swarmkv_cmd_new(4);
		sync_cmd->argv[0]=sdsnew("cache");
		sync_cmd->argv[1]=sdsnew("set");
		sync_cmd->argv[2]=sdsdup(obj->key);
		sync_cmd->argv[3]=obj_container_serialize(ctr);
	}
	else	//CACHE_DEL
	{
		sync_cmd=swarmkv_cmd_new(3);
		sync_cmd->argv[0]=sdsnew("cache");
		sync_cmd->argv[1]=sdsnew("del");
		sync_cmd->argv[2]=sdsdup(obj->key);
	}
	int iterator=0;
	struct swarmkv_node_addr *accessed_node=NULL;
	while((accessed_node=obj_container_iterate_accessed_nodes(ctr, &iterator)) && accessed_node)
	{
		store_cache_notify_one(store, sync_cmd, accessed_node);
	}
	swarmkv_cmd_free(sync_cmd);
}
int store_obj_setnx_accessed_node(struct swarmkv_module* mod_store, struct swarmkv_obj *obj, const struct swarmkv_node_addr *node)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);
	int added=0;
	added=obj_container_setnx_accessed_node(ctr, node);
	if(added)
	{
		struct swarmkv_cmd *sync_cmd=NULL;
		sync_cmd=swarmkv_cmd_new(4);
		sync_cmd->argv[0]=sdsnew("cache");
		sync_cmd->argv[1]=sdsnew("set");
		sync_cmd->argv[2]=sdsdup(obj->key);
		sync_cmd->argv[3]=obj_container_serialize(ctr);
		store_cache_notify_one(store, sync_cmd, node);
		swarmkv_cmd_free(sync_cmd);
		sync_cmd=NULL;
		return 1;
	}
	return 0;
}
struct swarmkv_obj *swarmkv_obj_new(enum swarmkv_obj_type type, const sds key)
{
	struct obj_container *ctr=obj_container_new(type, key);
	return &(ctr->obj);
}
void swarmkv_obj_free(struct swarmkv_obj *obj)
{
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);
	obj_container_free(ctr);
	return;
}
int swarmkv_obj_get_ttl(struct swarmkv_obj *obj)
{
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);
	if(!ctr->abs_timeout)
	{
		return -1;
	}
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	int ttl=ctr->abs_timeout - now.tv_sec;

	return ttl;
}


void store_add_obj(struct swarmkv_module *mod_store, struct swarmkv_obj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);
	obj_container_join(&(store->owned_obj_table), ctr);

	return;	

}
void store_remove_obj(struct swarmkv_module *mod_store, struct swarmkv_obj *obj)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=container_of(obj, struct obj_container, obj);
	obj_container_remove(&(store->owned_obj_table), ctr);
	return;
}

struct swarmkv_obj *store_lookup(struct swarmkv_module* mod_store, sds key)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=NULL;
	ctr=obj_container_find(&(store->owned_obj_table), key);
	if(ctr)
	{
		return &(ctr->obj);
	}
	else
	{
		return NULL;
	}
}
int store_expire_key(struct swarmkv_module* mod_store, sds key, int seconds)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=NULL;
	ctr=obj_container_find(&(store->owned_obj_table), key);
	if(!ctr)
	{
		return 0;
	}
	if(ctr->is_expiring)
	{
		timeouts_del(store->expires, &ctr->timeout_handle);
	}
	
	timeout_init(&ctr->timeout_handle, TIMEOUT_ABS);
	ctr->is_expiring=1;
		
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	ctr->abs_timeout=now.tv_sec+seconds;
	timeouts_add(store->expires, &ctr->timeout_handle, ctr->abs_timeout);
	store_cache_notify_all(mod_store, CACHE_SET, &ctr->obj);
	return 1;
}

int store_ttl_key(struct swarmkv_module* mod_store, sds key)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=NULL;
	int ttl=0;
	ctr=obj_container_find(&(store->owned_obj_table), key);
	if(!ctr)
	{
		return -2;
	}
	ttl=swarmkv_obj_get_ttl(&ctr->obj);
	return ttl;
}
struct swarmkv_obj *store_cache_lookup(struct swarmkv_module* mod_store, sds key)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=NULL;
	if(store->disable_cache)
	{
		return NULL;
	}
	ctr=obj_container_find(&(store->cached_obj_table), key);
	if(ctr)
	{
		return &(ctr->obj);
	}
	else
	{
		return NULL;
	}
}
void store_cache_flush(struct swarmkv_module *mod_store, enum cache_sync_op op, const sds key, const sds blob)
{
	struct swarmkv_store *store=module2store(mod_store);
	struct obj_container *ctr=NULL;	
	ctr=obj_container_find(&(store->cached_obj_table), key);
	HASH_FIND(hh, store->cached_obj_table, key, sdslen(key), ctr);
	switch (op)
	{
		case CACHE_SET:
			if(ctr)
			{
				obj_container_remove(&(store->cached_obj_table), ctr);
				obj_container_free(ctr);
				ctr=NULL;
			}
			ctr=obj_container_deserialize(blob);
			obj_container_join(&(store->cached_obj_table), ctr);
			break;
		case CACHE_DEL:
			if(ctr)
			{
				obj_container_remove(&(store->cached_obj_table), ctr);
				obj_container_free(ctr);
				ctr=NULL;
			}
			break;
		default:
			assert(0);
			break;
	}
	return;
}


struct swarmkv_module *swarmkv_store_new(send_cmd_func *send_cmd, void *handle_send_cmd, int disable_cache)
{
	struct swarmkv_store* store=ALLOC(struct swarmkv_store, 1);
	strncpy(store->module.name, "store", sizeof(store->module.name));
	store->module.mod_ctx=store;
	int error=0;
	store->expires=timeouts_open(0, &error);
	pthread_rwlock_init(&(store->module.rwlock), NULL);
	store->send_cmd=send_cmd;
	store->handle_send_cmd=handle_send_cmd;
	store->disable_cache=disable_cache;
	return &(store->module);
}

void swarmkv_store_free(struct swarmkv_module *module)
{
	struct swarmkv_store *store=module2store(module);
	struct obj_container *ctr=NULL, *tmp=NULL;
	pthread_rwlock_destroy(&(store->module.rwlock));
	HASH_ITER(hh, store->owned_obj_table, ctr, tmp)
	{
		obj_container_remove(&(store->owned_obj_table), ctr);
		obj_container_free(ctr);
	}

	HASH_ITER(hh, store->cached_obj_table, ctr, tmp)
	{
		obj_container_remove(&(store->cached_obj_table), ctr);
		obj_container_free(ctr);
	}
	timeouts_close(store->expires);
	store->expires=NULL;
	free(store);
	return;
}

void swarmkv_store_periodic(struct swarmkv_module *module)
{	
	struct swarmkv_store *store=module2store(module);
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	pthread_rwlock_wrlock(&(store->module.rwlock));
	timeouts_update(store->expires, now.tv_sec);
	struct timeout *t=NULL;
	struct obj_container *ctr=NULL;
	t=timeouts_get(store->expires);
	int i=0;
	while(t && i<100*1000)
	{
		ctr=container_of(t, struct obj_container, timeout_handle);
		assert(ctr->is_expiring);
		assert(ctr->is_in_table);
		obj_container_remove(&(store->owned_obj_table), ctr);
		store_cache_notify_all(module, CACHE_DEL, &ctr->obj);
		obj_container_free(ctr);
		t=timeouts_get(store->expires);
		i++;
	}
	pthread_rwlock_unlock(&(store->module.rwlock));
}

