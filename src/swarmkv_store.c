#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_net.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/prctl.h>


#define	MODULE_SWAMRKV_STORE	"swarmkv.store"


struct swarmkv_store
{	
	int my_node_id;

	struct key_route_table key_route;
	
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
	UT_array *nodes_have_kv_cached; //used by value owner for cache invalidate notification
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
int store_cache_cmd_exec(struct swarmkv_store *store, struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	int n_reply=0;
	struct obj_container *obj_table=store->cached_obj_table;
	struct obj_container* ctr=NULL;
	struct swarmkv_reply *r=NULL;

	sds key=cmd->args[0];
	HASH_FIND(hh, store->owned_obj_table, key, sdslen(key), ctr);
	if(ctr)
	{
		return n_reply;
	}
	r=ALLOC(struct swarmkv_reply, 1);
	int n_reply=0;
	switch (cmd->op)
	{
		case SWARMKV_OP_GET:			
			assert(cmd->n_args==1);
			if(ctr)
			{
				r->type=SWARMKV_REPLY_STRING;
				r->str=ALLOC(char, sdslen(ctr->obj.value));
				memcpy(r->str, ctr->obj.value, sdslen(ctr->obj.value));
			}
			else
			{
				r->type=SWARMKV_REPLY_NIL;
			}
			n_reply=1;
			*reply=r;
			break;
		default:
			free(r);
			r=0;
			assert(0);
			break;
	}
	return n_reply;

}

int swarmkv_store_exec(struct swarmkv_store *store, struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{	
	struct obj_container *obj_table=store->owned_obj_table;
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
				r->str=ALLOC(char, sdslen(ctr->obj.value));
				memcpy(r->str, ctr->obj.value, sdslen(ctr->obj.value));
			}
			else
			{
				r->type=SWARMKV_REPLY_NIL;
			}
			n_reply=1;
			*reply=r;
		case SWARMKV_OP_SET:
			assert(cmd->n_args==2);
			if(ctr)
			{
				sdsfree(ctr->obj.value);
				ctr->obj.value=sdsdup(cmd->args[1]);
			}
			else
			{
				ctr=ALLOC(swarmkv_object, 1);
				ctr->obj.key=sdsdup(key);
				ctr->obj.value=sdsdup(cmd->args[1]);
				HASH_ADD_KEYPTR(hh, obj_table, ctr->obj.key, sdslen(ctr->obj.key), ctr);
			}			
			r->type=SWARMKV_REPLY_STATUS;
			r->string=sdsnew("OK");
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
int value_owner_is_key_exisits(struct obj_container *hash_own_kv, const char *key, size_t keylen)
{
	return 0;
}

int lookup_value_owner(struct key_route_entry* hash_route_table, const char *key, size_t keylen)
{
	return 0;
}
int key_route_table_reg_key_if_not_exists(struct key_route_table* table, const char* key, size_t keylen, enum swarmkv_data_type type,  struct swarmkv_node* obj_owner)
{
	
}
void swarmkv_reply_free(struct swarmkv_reply *reply)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS
		case SWARMKV_REPLY_ERROR:
			free(reply->str);
			reply->str=NULL;
			break;
		case SWARMKV_REPLY_ARRAY:
			for(i=0; i<reply->n_element; i++)
			{
				swarmkv_reply_free(reply->element[i]);
				reply->element[i]=NULL;
			}
			break;
		default:
			break;
	}
	free(reply);
}


