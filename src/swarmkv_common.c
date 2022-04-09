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
struct swarmkv_reply* swarmkv_reply_new_string_fmt(const char *format, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STRING;
	va_list ap;
    va_start(ap,format);
	reply->len=vasprintf(&(reply->str), format, ap);
    va_end(ap);
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
void node_addr2reply(const struct swarmkv_node_addr *node, struct swarmkv_reply* reply[2])
{
	reply[0]=swarmkv_reply_new_string(node->ip_addr, strlen(node->ip_addr)+1);
	reply[1]=swarmkv_reply_new_integer(node->p2p_port);
	return;
}
void node_addr2sds(const struct swarmkv_node_addr *node, sds argv[2])
{
	argv[0]=sdsnew(node->ip_addr);
	argv[1]=sdsfromlonglong(node->p2p_port);
	return;
}
void sds2node_addr(const sds argv[2], struct swarmkv_node_addr *node_addr)
{
	memset(node_addr, 0, sizeof(struct swarmkv_node_addr));
	strncpy(node_addr->ip_addr, argv[0], sizeof(node_addr->ip_addr));
	node_addr->p2p_port=(unsigned int)atoll(argv[1]);
	return;
}
int swarmkv_node_compare(const struct swarmkv_node *node1, const struct swarmkv_node *node2)
{
	return memcmp(node1, node2, sizeof(struct swarmkv_node));
}
void swarmkv_node_copy(struct swarmkv_node *dst, const struct swarmkv_node *src)
{
	memcpy(dst, src, sizeof(struct swarmkv_node));
	return;
}
int swarmkv_node_addr_compare(const struct swarmkv_node_addr *addr1, const struct swarmkv_node_addr *addr2)
{
	return memcmp(addr1, addr2, sizeof(struct swarmkv_node_addr));
}
void swarmkv_node_addr_copy(struct swarmkv_node_addr *dst, const struct swarmkv_node_addr *src)
{
	memcpy(dst, src, sizeof(struct swarmkv_node_addr));
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
void swarmkv_reply_merge_array(struct swarmkv_reply **dst, struct swarmkv_reply *src)
{
	if(src->type==SWARMKV_REPLY_NIL) return;
	if(*dst==NULL)
	{
		*dst=swarmkv_reply_new_array(0);
	}
	assert((*dst)->type==SWARMKV_REPLY_ARRAY && src->type==SWARMKV_REPLY_ARRAY);
	(*dst)->elements=(struct swarmkv_reply **)realloc((*dst)->elements, ((*dst)->n_element+src->n_element)*sizeof(struct swarmkv_reply*));
	size_t i=0;
	for(i=0; i<src->n_element; i++)
	{
		(*dst)->elements[(*dst)->n_element+i]=src->elements[i];
		src->elements[i]=NULL;
	}
	(*dst)->n_element+=src->n_element;
	src->n_element=0;
	swarmkv_reply_free(src);
	return;
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
struct swarmkv_cmd* swarmkv_cmd_new(size_t argc)
{
	struct swarmkv_cmd* cmd=ALLOC(struct swarmkv_cmd, 1);
	cmd->argc=argc;
	cmd->argv=ALLOC(sds, argc);
	return cmd;
}
void swarmkv_cmd_free(struct swarmkv_cmd* p)
{
	size_t i=0;
	for(i=0; i<p->argc; i++)
	{
		sdsfree(p->argv[i]);
		p->argv[i]=NULL;
	}
	free(p->argv);
	p->argv=NULL;
	free(p);
}
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin)
{
	struct swarmkv_cmd* copy=ALLOC(struct swarmkv_cmd, 1);
	copy->argc=origin->argc;
	copy->argv=ALLOC(sds, copy->argc);
	size_t i=0;
	for(i=0; i<origin->argc; i++)
	{
		copy->argv[i]=sdsdup(origin->argv[i]);
	}
	return copy;
}
