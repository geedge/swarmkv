#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_net.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "mpack.h"
#include "uthash.h"
#include "utarray.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>



#define	MODULE_SWAMRKV_STORE	module_name_str("swarmkv.store")

enum swarmkv_obj_type
{
	OBJ_TYPE_STRING,
	OBJ_TYPE_HASH,
	OBJ_TYPE_THROTTLE,
	__SWARMKV_OBJ_TYPE_MAX
};

struct swarmkv_store
{	
	struct obj_container* owned_obj_table;
	struct obj_container* cached_obj_table;
};

struct swarmkv_object
{
	enum swarmkv_obj_type type;
	sds key;
	union
	{
		sds val_string;
		struct obj_container* hash;
	};
};
UT_icd ut_swarmkv_node_icd = {sizeof(struct swarmkv_node), NULL, NULL, NULL};

struct obj_container
{
	struct swarmkv_object obj;
	char is_in_table;
	UT_array *accessed_node_list; //used by value owner for cache invalidate notification
	UT_hash_handle hh;
};
static struct obj_container *obj_container_new(enum swarmkv_obj_type type, const sds key)
{
	struct obj_container *ctr=ALLOC(struct obj_container, 1);
	ctr->obj.type=type;
	ctr->obj.key=sdsdup(key);
	return ctr;
}
static struct obj_container *obj_container_find(struct obj_container **table, const sds key)
{
	struct obj_container *ctr=NULL;
	HASH_FIND(hh, *table, key, sdslen(key), ctr);
	return ctr;
}
static void obj_container_join(struct obj_container **table, struct obj_container *ctr)
{
//	struct obj_container *tmp=NULL;
//	HASH_FIND(hh, table, ctr->obj.key, sdslen(ctr->obj.key), tmp);
//	assert(tmp==NULL);
//	if(!tmp)
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
sds obj_container_serialize(const struct obj_container *ctr)
{
	sds blob=NULL;
	mpack_writer_t snapshot_mpack;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct obj_container *field=NULL, *tmp_field=NULL;

	mpack_writer_init_growable(&snapshot_mpack, &mpack_buff, &mpack_sz);
	mpack_build_map(&snapshot_mpack);

	mpack_write_cstr(&snapshot_mpack, "t");
	mpack_write_int(&snapshot_mpack, ctr->obj.type);

	mpack_write_cstr(&snapshot_mpack, "k");
	mpack_write_str(&snapshot_mpack, ctr->obj.key, sdslen(ctr->obj.key));

	mpack_write_cstr(&snapshot_mpack, "v");
	switch(ctr->obj.type)
	{
		case OBJ_TYPE_STRING:
			mpack_write_str(&snapshot_mpack, ctr->obj.val_string, sdslen(ctr->obj.val_string));
			break;
		case OBJ_TYPE_HASH:
			mpack_build_array(&snapshot_mpack);
			HASH_ITER(hh, ctr->obj.hash, field, tmp_field)
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
struct obj_container *obj_container_deserialize(const sds blob)
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
int node_cmp(const void *a, const void *b)
{
	return memcmp(a, b, sizeof(struct swarmkv_node));
}
static int obj_containter_setnx_accessed_node(struct obj_container *ctr, const struct swarmkv_node *node)
{
	struct swarmkv_node* found=NULL;
	if(!ctr->accessed_node_list)
	{
		utarray_new(ctr->accessed_node_list, &ut_swarmkv_node_icd);
	}
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
	struct swarmkv_node *first=NULL;
	if(!ctr->accessed_node_list)
	{
		utarray_new(ctr->accessed_node_list, &ut_swarmkv_node_icd);
	}
	first=(struct swarmkv_node *)utarray_front(ctr->accessed_node_list);
	*n_node=utarray_len(ctr->accessed_node_list);
	*node_list=ALLOC(struct swarmkv_node, *n_node);
	memcpy(*node_list, first, (*n_node)*sizeof(struct swarmkv_node));
}
int obj_container_remove_accessed_node(struct obj_container *ctr, const struct swarmkv_node *node)
{
	struct swarmkv_node *found=NULL;
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
		obj_container_remove(&(store->owned_obj_table), ctr);
		obj_container_free(ctr);
	}

	HASH_ITER(hh, store->cached_obj_table, ctr, tmp)
	{
		obj_container_remove(&(store->cached_obj_table), ctr);
		obj_container_free(ctr);
	}

	free(store);
	return;
}


const char *error_wrong_type="WRONGTYPE Operation against a key holding the wrong kind of value";
const char *error_wrong_number_of_arg="ERR wrong number of arguments";
const char *error_unknown_command="ERR unknown command";
const char *error_value_not_integer="ERR value is not an integer or out of range";

static struct swarmkv_reply *__store_exec(struct obj_container** table, const struct swarmkv_cmd *cmd, struct obj_container **out)
{
	struct swarmkv_reply *reply=NULL;
	struct obj_container* ctr=NULL;
	const sds key=cmd->args[0];
	ctr=obj_container_find(table, key);
	switch (cmd->op)
	{
		case SWARMKV_OP_GET:		
			if(cmd->n_args!=1)
			{
				reply=swarmkv_reply_new_error(error_wrong_number_of_arg);
				goto error_out;
			}
			if(ctr && ctr->obj.type!=OBJ_TYPE_STRING)
			{
				reply=swarmkv_reply_new_error(error_wrong_type);
				goto error_out;
			}
			if(ctr)
			{
				reply=swarmkv_reply_new_string(ctr->obj.val_string, sdslen(ctr->obj.val_string));
			}
			else
			{
				reply=swarmkv_reply_new_nil();
			}
			break;
		case SWARMKV_OP_SET:
			if(cmd->n_args!=2)
			{
				reply=swarmkv_reply_new_error(error_wrong_number_of_arg);
				goto error_out;
			}
			if(ctr && ctr->obj.type!=OBJ_TYPE_STRING)
			{
				reply=swarmkv_reply_new_error(error_wrong_type);
				goto error_out;
			}
			if(ctr)
			{
				sdsfree(ctr->obj.val_string);
				ctr->obj.val_string=sdsdup(cmd->args[1]);
			}
			else
			{
				ctr=obj_container_new(OBJ_TYPE_STRING, key);
				ctr->obj.val_string=sdsdup(cmd->args[1]);
				obj_container_join(table, ctr);
			}
			reply=swarmkv_reply_new_status("OK");
			break;
		case SWARMKV_OP_DEL:
			if(cmd->n_args!=1)
			{
				reply=swarmkv_reply_new_error(error_wrong_number_of_arg);
				goto error_out;
			}
			if(ctr)
			{
				obj_container_remove(table, ctr);
				reply=swarmkv_reply_new_integer(1);
			}
			else
			{
				reply=swarmkv_reply_new_integer(0);
			}	
			break;
		case SWARMKV_OP_INCRBY:
			if(cmd->n_args!=2)
			{
				reply=swarmkv_reply_new_error(error_wrong_number_of_arg);
				goto error_out;
			}
			if(ctr && ctr->obj.type!=OBJ_TYPE_STRING)
			{
				reply=swarmkv_reply_new_error(error_wrong_type);
				goto error_out;
			}
			if(ctr)
			{
				long long value=0, delta=0;
				value=atoll(ctr->obj.val_string);
				delta=atoll(cmd->args[1]);
				value+=delta;
				sdsfree(ctr->obj.val_string);
				ctr->obj.val_string=sdsfromlonglong(value);
			}
			else
			{
				ctr=obj_container_new(OBJ_TYPE_STRING, key);
				ctr->obj.val_string=sdsdup(cmd->args[1]);
				obj_container_join(table, ctr);
			}
			swarmkv_reply_new_integer(atoll(ctr->obj.val_string));
			break;
		default:
			assert(0);
			reply=swarmkv_reply_new_error(error_unknown_command);
			break;
	}
	*out=ctr;
error_out:
	return reply;

}

struct swarmkv_reply *swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, 
	 const struct swarmkv_node* accessing_node, struct swarmkv_cmd **out_sync_cmd, struct swarmkv_node **accessed_nodes, size_t *n_accessed_node)
{	
	struct swarmkv_reply *reply=NULL;
	struct obj_container *ctr=NULL;
	struct swarmkv_cmd *sync_cmd=NULL;
	int first_read=0;
	reply=__store_exec(&(store->owned_obj_table), cmd, &ctr);

	if(!ctr)
	{
		goto not_update_cache;
	}
	
	if(ctr->is_in_table && accessing_node)
	{
		first_read=obj_containter_setnx_accessed_node(ctr, accessing_node);
	}
	
	if(is_store_read_operator(cmd->op))
	{
		if(accessing_node && first_read)
		{
			sync_cmd=swarmkv_cmd_new(SWARMKV_OP_CACHE_SET, 2);
			sync_cmd->args[0]=sdsdup(cmd->args[0]);
			sync_cmd->args[1]=obj_container_serialize(ctr);
			*accessed_nodes=ALLOC(struct swarmkv_node, 1);
			memcpy(*accessed_nodes, accessing_node, sizeof(struct swarmkv_node));
			*n_accessed_node=1;
		}
	}
	else if(is_store_del_operator(cmd->op))
	{
		sync_cmd=swarmkv_cmd_new(SWARMKV_OP_CACHE_DEL, 1);
		sync_cmd->args[0]=sdsdup(cmd->args[0]);
		obj_container_get_accessed_nodes(ctr, accessed_nodes, n_accessed_node);
	}
	else
	{
		sync_cmd=swarmkv_cmd_new(SWARMKV_OP_CACHE_SET, 2);
		sync_cmd->args[0]=sdsdup(cmd->args[0]);
		sync_cmd->args[1]=obj_container_serialize(ctr);
		obj_container_get_accessed_nodes(ctr, accessed_nodes, n_accessed_node);
	}
	
	*out_sync_cmd=sync_cmd;
	if(!ctr->is_in_table)
	{
		obj_container_free(ctr);
	}
not_update_cache:	
	return reply;
}
int swarmkv_store_cache_read(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	int is_cache_hit=0;
	struct obj_container* ctr=NULL;
	struct swarmkv_reply *r=NULL;
	
	assert(is_store_read_operator(cmd->op));
	
	r=__store_exec(&(store->cached_obj_table), cmd, &ctr);
	if(r->type==SWARMKV_REPLY_NIL)
	{
		is_cache_hit=0;
		swarmkv_reply_free(r);
		r=NULL;
	}
	else
	{
		*reply=r;
	}
	if(ctr)
	{
		assert(ctr->is_in_table);
	}
	return is_cache_hit;
}

void swarmkv_store_update_cache(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct obj_container *ctr=NULL;
	sds key=cmd->args[0];
	struct obj_container **table=&(store->cached_obj_table);

	ctr=obj_container_find(table, key);
	switch (cmd->op)
	{
		case SWARMKV_OP_CACHE_SET:
			if(ctr)
			{
				obj_container_remove(table, ctr);
				obj_container_free(ctr);
				ctr=NULL;
			}
			ctr=obj_container_deserialize(cmd->args[1]);
			obj_container_join(table, ctr);			
			*reply=swarmkv_reply_new_status("OK");
			break;
		case SWARMKV_OP_CACHE_DEL:
			if(ctr)
			{
				obj_container_remove(table, ctr);
				obj_container_free(ctr);
				ctr=NULL;
				*reply=swarmkv_reply_new_integer(1);
			}
			else
			{
				*reply=swarmkv_reply_new_integer(0);
			}
			break;
		default:
			assert(0);
			*reply=swarmkv_reply_new_error(error_unknown_command);
			break;
	}
	return;
}

