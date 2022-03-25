#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"

#include "uthash.h"

#include <stdlib.h>
#include <stdio.h>

struct swarmkv_reply* swarmkv_reply_new_string(const char* str, size_t sz)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STRING;
	reply->len=sz;
	reply->str=ALLOC(char, sz);
	memcpy(reply->str, str, sz);
	return reply;
}
struct swarmkv_reply* swarmkv_reply_new_array(size_t n_element)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ARRAY;
	reply->n_element=n_element;
	reply->elements=ALLOC(struct swarmkv_reply*, n_element);
	return reply;

}
struct swarmkv_reply* swarmkv_reply_new_integer(long long integer)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_INTEGER;
	reply->integer=integer;
	return reply;
}
struct swarmkv_reply* swarmkv_reply_new_nil(void)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_NIL;
	return reply;
}
struct swarmkv_reply* swarmkv_reply_new_status(const char *format, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STATUS;
	va_list ap;
    va_start(ap,format);
	reply->len=vasprintf(&(reply->str), format, ap);
    va_end(ap);
		return reply;
}
struct swarmkv_reply* swarmkv_reply_new_error(const char *format, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ERROR;
	reply->str=ALLOC(char, reply->len);
	va_list ap;
    va_start(ap,format);
	reply->len=vasprintf(&(reply->str), format, ap);
    va_end(ap);
	
	return reply;
}
void node2reply(const struct swarmkv_node *node, struct swarmkv_reply* reply[2])
{
	reply[0]=swarmkv_reply_new_string(node->ip_addr, strlen(node->ip_addr)+1);
	reply[1]=swarmkv_reply_new_integer(node->p2p_port);
	return;
}
void sds2node(const sds args[2], struct swarmkv_node *node)
{
	strncpy(node->ip_addr, args[0], sizeof(node->ip_addr));
	node->p2p_port=(unsigned int)atoll(args[1]);
	return;
}
struct swarmkv_reply *swarmkv_reply_dup(const struct swarmkv_reply *origin)
{
	struct swarmkv_reply *copy=ALLOC(struct swarmkv_reply, 1);
	copy->type=origin->type;
	size_t i=0;
	switch (origin->type)
	{
		case SWARMKV_REPLY_INTEGER:
			copy->integer=origin->integer;
			break;
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
			copy->len=origin->len;
			copy->str=ALLOC(char, origin->len);
			memcpy(copy->str, origin->str, origin->len);
			break;
		case SWARMKV_REPLY_ARRAY:
			copy->n_element=origin->n_element;
			copy->elements=ALLOC(struct swarmkv_reply*, origin->n_element);
			for(i=0; i<origin->n_element; i++)
			{
				copy->elements[i]=swarmkv_reply_dup(origin->elements[i]);
			}
			break;
		default:
			break;

	}
	return copy;
}

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
int is_store_read_operator(enum swarmkv_operator op)
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
int is_store_del_operator(enum swarmkv_operator op)
{
	int ret=0;
	switch(op)
	{
		case SWARMKV_OP_DEL:
		case SWARMKV_OP_HDEL:
			ret=1;
		default:
			break;
	}
	return ret;

}
int is_keyspace_operator(enum swarmkv_operator op)
{
	int ret=0;
	switch(op)
	{
		case SWARMKV_OP_KEYSPACE_SET:
		case SWARMKV_OP_KEYSPACE_SETNXGET:
		case SWARMKV_OP_KEYSPACE_GET:
		case SWARMKV_OP_KEYSPACE_DEL:
		case SWARMKV_OP_KEYSPACE_GETSLOTKEYS:
			ret=1;
			break;
		default:
			ret=0;
			break;
	}
	return ret;
}
int is_cache_operator(enum swarmkv_operator op)
{
	int ret=0;
	switch(op)
	{
		case SWARMKV_OP_CACHE_DEL:
		case SWARMKV_OP_CACHE_SET:
			ret=1;
			break;
		default:
			ret=0;
			break;
	}
	return ret;
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
static const char* g_swarmkv_common_op2str[__SWARMKV_OP_MAX];
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
	
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_SET]="KEYSPACE SET";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_SETNXGET]="KEYSPACE SETNXGET";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_GET]="KEYSPACE GET";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_DEL]="KEYSPACE DEL";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_GETSLOTKEYS]="KEYSPACE GETSLOTKEYS";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_SETSLOT]="KEYSPACE SETSLOT";
	g_swarmkv_common_op2str[SWARMKV_OP_KEYSPACE_KEYS]="KEYSPACE KEYS";
	
	g_swarmkv_common_op2str[SWARMKV_OP_CACHE_SET]="SYNC SET";
	g_swarmkv_common_op2str[SWARMKV_OP_CACHE_DEL]="SYNC DEL";

	g_swarmkv_common_op2str[SWARMKV_OP_CLUSTER_KEYS]="CLUSTER KEYS";
	g_swarmkv_common_op2str[SWARMKV_OP_CLUSTER_ADDNODE]="CLUSTER ADDNODE";
	int i=0;
	struct str2op *p=NULL;
	for(i=0; i<__SWARMKV_OP_MAX; i++)
	{
		p=ALLOC(struct str2op, 1);
		strcpy(p->str, g_swarmkv_common_op2str[i]);
		p->op=i;
		HASH_ADD(hh, g_swarmkv_common_str2op_map, str, strlen(p->str), p);
	}
	g_swarmkv_common_init=1;
}
int has_sub_operator(const char* op_str)
{
	size_t i=0;
	char *p=NULL;
	for(i=0; i<__SWARMKV_OP_MAX; i++)
	{
		p=strcasestr(g_swarmkv_common_op2str[i], op_str);
		if(p && p==g_swarmkv_common_op2str[i] && *(p+strlen(op_str))==' ')
		{
			return 1;
		}
		else
		{
			continue;
		}
	}
	return 0;
}
int command_str2op(const char* op_str, const char *sub_op_str, enum swarmkv_operator *op)
{
	size_t i=0;
	char tmp[256];
	if(sub_op_str)
	{
		snprintf(tmp, sizeof(tmp), "%s %s", op_str, sub_op_str);
	}
	else
	{
		snprintf(tmp, sizeof(tmp), "%s", op_str);
	}
	for(i=0; i<__SWARMKV_OP_MAX; i++)
	{
		if(!strcasecmp(g_swarmkv_common_op2str[i], tmp))
		{
			*op=i;
			return 0;
		}
	}
	return -1;
	
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
enum swarmkv_operator swarmkv_cmd_str2op(const char* opstr, size_t len)
{
	struct str2op *p=NULL;
	HASH_FIND(hh, g_swarmkv_common_str2op_map, opstr, len, p);
	return p->op;	
}


