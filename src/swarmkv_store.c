#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_net.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "uthash.h"
#include "utarray.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>



#define	MODULE_SWAMRKV_STORE	module_name_str("swarmkv.store")

enum swarmkv_obj_type
{
	STRING,
	HASH,
	THROTTLE,
	__SWARMKV_DATA_TYPE_MAX
};

struct swarmkv_store
{	
	struct obj_container *owned_obj_table;
	struct obj_container *cached_obj_table;
};

struct swarmkv_object
{
	enum swarmkv_obj_type type;
	sds key;
	sds value;
};
UT_icd ut_swarmkv_node_icd = {sizeof(struct swarmkv_node), NULL, NULL, NULL};

struct obj_container
{
	struct swarmkv_object obj;
	long long version;
	UT_array *accessed_node_list; //used by value owner for cache invalidate notification
	UT_hash_handle hh;
};
struct obj_container *obj_container_new()
{
	struct obj_container *ctr=ALLOC(struct obj_container, 1);
	utarray_new(ctr->accessed_node_list, &ut_swarmkv_node_icd);
	
	return ctr;
}

static void obj_container_free(struct obj_container *ctr)
{
	utarray_free(ctr->accessed_node_list);
	sdsfree(ctr->obj.key);
	sdsfree(ctr->obj.value);
	free(ctr);
	return;
}
int node_cmp(const void *a, const void *b)
{
	return memcmp(a, b, sizeof(struct swarmkv_node));
}
static int obj_containter_setnx_accessed_node(struct obj_container *ctr, const struct swarmkv_node *node)
{
	struct swarmkv_node* found=NULL;
	found=(struct swarmkv_node*)utarray_find(ctr->accessed_node_list, node, node_cmp);
	if(found)
	{
		return 0;
	}
	utarray_push_back(ctr->accessed_node_list, node);
	utarray_sort(ctr->accessed_node_list, node_cmp);
	return 1;
}
void obj_container_get_accessed_nodes(struct obj_container *ctr, struct swarmkv_node **node_list, size_t *n_node)
{
	struct swarmkv_node *first=(struct swarmkv_node *)utarray_front(ctr->accessed_node_list);
	*n_node=utarray_len(ctr->accessed_node_list);
	*node_list=ALLOC(struct swarmkv_node, *n_node);
	memcpy(*node_list, first, (*n_node)*sizeof(struct swarmkv_node));
}
int obj_container_remove_accessed_node(struct obj_container *ctr, const struct swarmkv_node *node)
{
	struct swarmkv_node *found=NULL;
	size_t found_idx=0;
	
	found=utarray_find(ctr->accessed_node_list, node, node_cmp);
	if (found)
	{
		found_idx=utarray_eltidx(ctr->accessed_node_list, found);
		utarray_erase(ctr->accessed_node_list, found_idx, 1);
		return 1;
	}
	return 0;
}


void swarmkv_store_cache_sync(struct swarmkv_store *store, const struct swarmkv_cmd *sync_cmd, struct swarmkv_reply **reply)
{
	struct obj_container* ctr=NULL;
	assert(sync_cmd->op == SWARMKV_OP_CLUSTER_SYNC);
	long long original_obj_version=atoll(sync_cmd->args[0]);
	struct swarmkv_cmd __cmd;
	//a hack cmd for avoiding memory copy.
	__cmd.op=*(enum swarmkv_operator*)(sync_cmd->args[1]);
	__cmd.n_args=sync_cmd->n_args-2;
	__cmd.args=sync_cmd->args+2;
	
	const struct swarmkv_cmd *cmd=NULL;
	cmd=&__cmd;
	sds key=cmd->args[0];
	
	HASH_FIND(hh, store->cached_obj_table, key, sdslen(key), ctr);
	switch (cmd->op)
	{
		case SWARMKV_OP_SET:
			if(ctr)
			{
				sdsfree(ctr->obj.value);
				ctr->obj.value=sdsdup(cmd->args[1]);
				ctr->version=original_obj_version;
			}
			else
			{
				ctr=obj_container_new();
				ctr->obj.key=sdsdup(key);
				ctr->obj.value=sdsdup(cmd->args[1]);
				ctr->version=original_obj_version;
				HASH_ADD_KEYPTR(hh, store->cached_obj_table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
			}
			break;
		case SWARMKV_OP_DEL:
			if(ctr)
			{
				HASH_DELETE(hh, store->cached_obj_table, ctr);
				obj_container_free(ctr);
				ctr=NULL;
			}
			else
			{
				goto out_of_sync;
			}
			break;
		case SWARMKV_OP_INCRBY:
			if(ctr)
			{
				if(original_obj_version - ctr->version==1)
				{
					long long value=0, delta=0;
					value=atoll(ctr->obj.value);
					delta=atoll(cmd->args[1]);
					value+=delta;
					sdsfree(ctr->obj.value);
					ctr->obj.value=sdsfromlonglong(value);
					ctr->version++;
				}
				else
				{
					goto out_of_sync;
				}
			}
			else
			{
				long long value=0, delta=0;
				ctr=obj_container_new();
				ctr->obj.key=sdsdup(key);
				value+=delta;
				ctr->obj.value=sdsfromlonglong(value);
				ctr->version=original_obj_version;
				HASH_ADD_KEYPTR(hh, store->cached_obj_table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
			}
			break;
		default:
			assert(0);
			break;
	}
	*reply=swarmkv_reply_new_status("OK");
	return;
out_of_sync:	
	if(ctr)
	{
		HASH_DELETE(hh, store->cached_obj_table, ctr);
		obj_container_free(ctr);
	}
	*reply=swarmkv_reply_new_status("out of sync");
	return;
}

int swarmkv_store_cache_read(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	int is_cache_hit=0;
	struct obj_container* ctr=NULL;
	struct swarmkv_reply *r=NULL;

	sds key=cmd->args[0];
	HASH_FIND(hh, store->cached_obj_table, key, sdslen(key), ctr);
	if(!ctr)
	{
		return is_cache_hit;
	}
	is_cache_hit=1;
	r=ALLOC(struct swarmkv_reply, 1);
	switch (cmd->op)
	{
		case SWARMKV_OP_GET:			
			assert(cmd->n_args==1);
			if(ctr)
			{
				r->type=SWARMKV_REPLY_STRING;
				r->len=sdslen(ctr->obj.value);
				r->str=ALLOC(char, r->len);
				memcpy(r->str, ctr->obj.value, sdslen(ctr->obj.value));
				*reply=r;
			}		
			break;
		default:
			free(r);
			r=0;
			assert(0);
			break;
	}
	return is_cache_hit;
}
struct swarmkv_store* swarmkv_store_new()
{
	struct swarmkv_store* store=ALLOC(struct swarmkv_store, 1);
	return store;
}
void swarmkv_store_free(struct swarmkv_store* store)
{
	struct obj_container *ctr=NULL, *tmp=NULL;
	HASH_ITER(hh, store->owned_obj_table, ctr, tmp)
	{
		HASH_DELETE(hh, store->owned_obj_table, ctr);
		obj_container_free(ctr);
	}

	HASH_ITER(hh, store->cached_obj_table, ctr, tmp)
	{
		HASH_DELETE(hh, store->cached_obj_table, ctr);
		obj_container_free(ctr);
	}

	free(store);
	return;
}

struct swarmkv_cmd* sync_cmd_new(long long obj_version, const struct swarmkv_cmd* origin_cmd)
{
	struct swarmkv_cmd *sync_cmd=NULL;
	size_t i=0;
	sync_cmd=swarmkv_cmd_new(SWARMKV_OP_CLUSTER_SYNC, origin_cmd->n_args+2);
	sync_cmd->args[0]=sdsfromlonglong(obj_version);
	sync_cmd->args[1]=sdsnewlen(&(origin_cmd->op), sizeof(origin_cmd->op));
	for(i=0; i<origin_cmd->n_args; i++)
	{
		sync_cmd->args[i+2]=sdsdup(origin_cmd->args[i]);
	}
	return sync_cmd;
}
void swarmkv_store_process_sync_reply(struct swarmkv_store *store, sds key, const struct swarmkv_node* peer, const struct swarmkv_reply* reply, struct swarmkv_cmd **full_sync_cmd)
{
	struct swarmkv_cmd *out=NULL;
	assert(reply->type==SWARMKV_REPLY_STATUS);
	if(0==strncasecmp(reply->str, "OK", reply->len))
	{
		return;
	}
	struct obj_container* ctr=NULL;
	HASH_FIND(hh, store->owned_obj_table, key, sdslen(key), ctr);
	if(!ctr)
	{
		return;
	}
	obj_container_remove_accessed_node(ctr, peer);
	switch(ctr->obj.type)
	{
		case STRING:
			out=swarmkv_cmd_new(SWARMKV_OP_SET, 2);
			out->args[0]=sdsdup(ctr->obj.key);
			out->args[1]=sdsdup(ctr->obj.value);
			break;
		case HASH:
			//todo
			break;
		default:
			assert(0);
			break;
	}
	if(out)
	{
		*full_sync_cmd=out;
	}
	return;
}
int swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply, 
	 const struct swarmkv_node* accessing_node, struct swarmkv_cmd **sync_cmd, struct swarmkv_node **accessed_nodes, size_t *n_accessed_node)
{	
	struct swarmkv_reply *r=NULL;
	struct obj_container* ctr=NULL;
	sds key=cmd->args[0];
	HASH_FIND(hh, store->owned_obj_table, key, sdslen(key), ctr);
	int n_reply=0;
	switch (cmd->op)
	{
		case SWARMKV_OP_GET:		
			assert(cmd->n_args==1);
			if(ctr)
			{
				r=swarmkv_reply_new_string(ctr->obj.value, sdslen(ctr->obj.value));
			}
			else
			{
				r=swarmkv_reply_new_nil();
			}
			n_reply=1;
			break;
		case SWARMKV_OP_SET:
			assert(cmd->n_args==2);
			if(ctr)
			{
				sdsfree(ctr->obj.value);
				ctr->obj.value=sdsdup(cmd->args[1]);
				ctr->version++;
			}
			else
			{
				ctr=obj_container_new();
				ctr->obj.key=sdsdup(key);
				ctr->obj.value=sdsdup(cmd->args[1]);
				HASH_ADD_KEYPTR(hh, store->owned_obj_table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
			}
			r=swarmkv_reply_new_status("OK");
			//write operation triggers cache sync message. 
			obj_container_get_accessed_nodes(ctr, accessed_nodes, n_accessed_node);
			*sync_cmd=sync_cmd_new(ctr->version, cmd);
			break;
		case SWARMKV_OP_DEL:
			if(ctr)
			{
				ctr->version++;
				HASH_DELETE(hh, store->owned_obj_table, ctr);
				r=swarmkv_reply_new_integer(1);
				obj_container_get_accessed_nodes(ctr, accessed_nodes, n_accessed_node);				
				*sync_cmd=sync_cmd_new(ctr->version, cmd);
				obj_container_free(ctr);
				ctr=NULL;
			}
			else
			{
				r=swarmkv_reply_new_integer(0);
			}
			
			break;
		default:
			assert(0);
			break;
	}
	*reply=r;
	//Record accessing node
	if(ctr && accessing_node)
	{
		obj_containter_setnx_accessed_node(ctr, accessing_node);
	}
	return n_reply;
}

