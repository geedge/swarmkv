#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_net.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "uthash.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>



#define	MODULE_SWAMRKV_STORE	"swarmkv.store"


struct swarmkv_store
{	
	struct obj_container *owned_obj_table;
	struct obj_container *cached_obj_table;
};

struct swarmkv_object
{
	enum swarmkv_data_type type;
	sds key;
	sds value;
};
struct obj_container
{
	struct swarmkv_object obj;
//	UT_array *nodes_have_kv_cached; //used by value owner for cache invalidate notification
	UT_hash_handle hh;
};

int is_read_operator(enum swarmkv_operator op)
{
	int ret=0;
	switch(op)
	{
		case SWARMKV_OP_GET:
		case SWARMKV_OP_HGET:
		case SWARMKV_OP_HGETALL:
			ret=1;
		default:
			break;
	}
	return ret;
}
int swarmkv_store_cache_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	int n_reply=0;
	struct obj_container* ctr=NULL;
	struct swarmkv_reply *r=NULL;

	sds key=cmd->args[0];
	HASH_FIND(hh, store->owned_obj_table, key, sdslen(key), ctr);
	if(ctr)
	{
		return n_reply;
	}
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
				n_reply=1;
				*reply=r;
			}		
			break;
		default:
			free(r);
			r=0;
			assert(0);
			break;
	}
	return n_reply;

}
struct swarmkv_store* swarmkv_store_new()
{
	struct swarmkv_store* store=ALLOC(struct swarmkv_store, 1);
	return store;
}
static void obj_container_free(struct obj_container *ctr)
{
	sdsfree(ctr->obj.key);
	sdsfree(ctr->obj.value);
	free(ctr);
	return;
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


int swarmkv_store_exec(struct swarmkv_store *store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{	
	struct swarmkv_reply *r=ALLOC(struct swarmkv_reply, 1);
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
				r->type=SWARMKV_REPLY_STRING;
				r->len=sdslen(ctr->obj.value);
				r->str=ALLOC(char,r->len);
				memcpy(r->str, ctr->obj.value, r->len);
			}
			else
			{
				r->type=SWARMKV_REPLY_NIL;
			}
			n_reply=1;
			*reply=r;
			break;
		case SWARMKV_OP_SET:
			assert(cmd->n_args==2);
			if(ctr)
			{
				sdsfree(ctr->obj.value);
				ctr->obj.value=sdsdup(cmd->args[1]);
			}
			else
			{
				ctr=ALLOC(struct obj_container, 1);
				ctr->obj.key=sdsdup(key);
				ctr->obj.value=sdsdup(cmd->args[1]);
				HASH_ADD_KEYPTR(hh, store->owned_obj_table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
			}			
			r->type=SWARMKV_REPLY_STATUS;
			r->len=strlen("OK")+1;
			r->str=ALLOC(char, r->len);
			strcpy(r->str, "OK");
			*reply=r;
			break;
		default:
			free(r);
			r=NULL;
			assert(0);
			break;
	}
	
	return n_reply;
}

