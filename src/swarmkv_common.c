#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"

#include "uthash.h"

#include <stdlib.h>

void swarmkv_reply_free(struct swarmkv_reply *reply)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
			free(reply->str);
			reply->str=NULL;
			break;
		case SWARMKV_REPLY_ARRAY:
			for(i=0; i<reply->n_element; i++)
			{
				swarmkv_reply_free(reply->elements[i]);
				reply->elements[i]=NULL;
			}
			free(reply->elements);
			break;
		default:
			break;
	}
	free(reply);
}

struct swarmkv_cmd* swarmkv_cmd_new(enum swarmkv_operator op, size_t n_args)
{
	struct swarmkv_cmd* cmd=ALLOC(struct swarmkv_cmd, 1);
	cmd->op=op;
	cmd->n_args=n_args;
	cmd->args=ALLOC(sds, n_args);
	return cmd;
}
void swarmkv_cmd_free(struct swarmkv_cmd* p)
{
	size_t i=0;
	for(i=0; i<p->n_args; i++)
	{
		sdsfree(p->args[i]);
		p->args[i]=NULL;
	}
	free(p->args);
	p->args=NULL;
	free(p);
}
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin)
{
	struct swarmkv_cmd* copy=ALLOC(struct swarmkv_cmd, 1);
	copy->op=origin->op;
	copy->n_args=origin->n_args;
	copy->args=ALLOC(sds, copy->n_args);
	size_t i=0;
	for(i=0; i<origin->n_args; i++)
	{
		copy->args[i]=sdsdup(origin->args[i]);
	}
	return copy;
}
static const char* g_swarmkv_common_op2str[__SWARMKV_CMD_MAX];
struct str2op
{
	char str[32];
	enum swarmkv_operator op;
	UT_hash_handle hh;
};
static struct str2op* g_swarmkv_common_str2op_map=NULL;
static int g_swarmkv_common_init=0;
void swarmkv_common_global_init(void)
{
	if(g_swarmkv_common_init) return;
	
	g_swarmkv_common_op2str[SWARMKV_OP_GET]="GET";
	g_swarmkv_common_op2str[SWARMKV_OP_SET]="SET";
	g_swarmkv_common_op2str[SWARMKV_OP_DEL]="DEL";
	g_swarmkv_common_op2str[SWARMKV_OP_HSET]="HSET";
	g_swarmkv_common_op2str[SWARMKV_OP_HGET]="HGET";
	g_swarmkv_common_op2str[SWARMKV_OP_HGETALL]="HGETALL";	
	g_swarmkv_common_op2str[SWARMKV_OP_HDEL]="HDEL";
	g_swarmkv_common_op2str[SWARMKV_OP_INCRBY]="INCRBY";
	g_swarmkv_common_op2str[SWARMKV_OP_DECRBY]="DECRBY";
	g_swarmkv_common_op2str[SWARMKV_OP_THROTTLESET]="THROTTLESET";
	g_swarmkv_common_op2str[SWARMKV_OP_EXPIRE]="EXPIRE";
	g_swarmkv_common_op2str[SWARMKV_OP_CLUSTER_OBJ_OWNER]="CLUSTER OBJECT_OWNER";	
	g_swarmkv_common_op2str[SWARMKV_OP_CLUSTER_SYNC]="SYNC";	
	int i=0;
	struct str2op *p=NULL;
	for(i=0; i<__SWARMKV_CMD_MAX; i++)
	{
		p=ALLOC(struct str2op, 1);
		strcpy(p->str, g_swarmkv_common_op2str[i]);
		p->op=i;
		HASH_ADD_STR(g_swarmkv_common_str2op_map, str, p);
	}
	g_swarmkv_common_init=1;
}
void swarmkv_common_global_deinit(void)
{
	struct str2op *p=NULL, *tmp=NULL;
	HASH_ITER(hh, g_swarmkv_common_str2op_map, p, tmp)
	{
		HASH_DEL(g_swarmkv_common_str2op_map, p);
		free(p);
	}
}

const char* swarmkv_cmd_op2str(enum swarmkv_operator op)
{

	return g_swarmkv_common_op2str[op];
}
enum swarmkv_operator swarmkv_cmd_str2op(const char* opstr)
{
	struct str2op *p=NULL;
	HASH_FIND_STR(g_swarmkv_common_str2op_map, opstr, p);
	return p->op;	
}


