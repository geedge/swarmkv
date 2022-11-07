#include "swarmkv_common.h"
#include "swarmkv_store.h"
#include "swarmkv_error.h"
#include "swarmkv_utils.h"
#include "utarray.h"

#include <stdlib.h>
#include <assert.h>

UT_icd ut_array_matched_reply = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};

struct pattern_match_arg
{
	sds pattern;
    UT_array *matched_replies;
};

enum cmd_exec_result crdt_pull_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* CRDT PULL key */
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	char *blob=NULL;
	size_t blob_sz=0;
	assert(accessing_node!=NULL);//should never invoked by local
	obj=store_lookup(mod_store, key);
	if(obj)
	{	
		sobj_add_replica_node(obj, accessing_node);
		if(obj->type!=OBJ_TYPE_UNDEFINED)
		{
			sobj_serialize(obj, &blob, &blob_sz);
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
/* CRDT PUSH key blob*/
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	const sds blob=cmd->argv[3];	
	assert(accessing_node!=NULL);//should never invoked by local
	obj=store_lookup(mod_store, key);
	uuid_t uuid;
	store_get_uuid(mod_store, uuid);
	if(obj)
	{	
		sobj_merge_blob(obj, blob, sdslen(blob), uuid);
		*reply=swarmkv_reply_new_integer(1);
	}
	else
	{
		*reply=swarmkv_reply_new_integer(0);
	}	
	return FINISHED;
}

enum cmd_exec_result crdt_del_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
    /* CRDT DEL <key> */
	sds key=cmd->argv[2];
	struct sobj *obj=NULL;
	obj=store_lookup(mod_store, key);
	if(obj)
	{
		store_remove_obj(mod_store, obj);
		sobj_free(obj);
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
	struct sobj *obj=NULL;
	const sds key=cmd->argv[2];
	assert(accessing_node!=NULL);//should never invoked by local

    node_t replica_node;
    node_init_from_sds(&replica_node, cmd->argv[3]);

	obj=store_lookup(mod_store, key);
	int added=0;
	if(obj)
	{
		added=sobj_add_replica_node(obj, &replica_node);
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
    store_iterate_sobj(mod_store, pattern_match_function, &cb_arg);
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
	struct sobj *obj=NULL;
    int n_matched=0,i=0;
    UT_array *matched_replies;
	assert(accessing_node!=NULL);//should never invoked by local

	const sds key=cmd->argv[2];
	obj=store_lookup(mod_store, key);
	if(obj)
	{
        utarray_new(matched_replies, &ut_array_matched_reply);
        int iterator=0;
        node_t *replica_node=NULL;
        while((replica_node=store_iterate_replica_node(obj, &iterator)) && replica_node)
        {
            struct swarmkv_reply *r=NULL;
            r=swarmkv_reply_new_node(replica_node, 0);
            utarray_push_back(matched_replies, &r);
        }
        n_matched=utarray_len(matched_replies);
        if(n_matched>0)
        {
            *reply=swarmkv_reply_new_array(n_matched);
            for(i=0; i<n_matched; i++)
            {
                (*reply)->elements[i]=*(struct swarmkv_reply **)utarray_eltptr(matched_replies, i);
            }
        }
        else
        {
            *reply=swarmkv_reply_new_nil();
        }
        utarray_free(matched_replies);
	}
	else
	{
		*reply=swarmkv_reply_new_nil();
	}

	return FINISHED;
}

