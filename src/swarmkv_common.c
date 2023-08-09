#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"

#include "uthash.h"
#include "xxhash.h"
#include "cJSON.h"

#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>

int str2integer(const char *str, long long *integer)
{
	char *endptr=NULL;
	*integer=strtol(str, &endptr, 10);
	if(*endptr!='\0')
	{
		return -1;		
	}
	return 0;
}
/*
void swarmkv_reply_print(const struct swarmkv_reply *reply, FILE* stream)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_INTEGER:
			fprintf(stream, "(integer) %lld\n", reply->integer);
			break;
		case SWARMKV_REPLY_NIL:
			fprintf(stream, "(nil)\n");
			break;
		case SWARMKV_REPLY_ERROR:
			fprintf(stream, "(error) %s\n", reply->str);
			break;
		case SWARMKV_REPLY_NODE:
		case SWARMKV_REPLY_STATUS:
			fprintf(stream, "%s\n", reply->str);
			break;
		case SWARMKV_REPLY_STRING:
			fprintf(stream, "\"%.*s\"\n", reply->len, reply->str);
			break;
		case SWARMKV_REPLY_VERBATIM:
			frpintf(stream, "%s", reply->len, reply->str);
			break;
		case SWARMKV_REPLY_ARRAY:
			for(i=0; i<reply->n_element; i++)
			{
				fprintf(stream, "%zu) ", i);
				swarmkv_reply_print(reply->elements[i], stream);
			}
			break;
		default:
			break;

	}
}*/
sds swarmkv_reply_format(const struct swarmkv_reply *r, char *prefix) {
    sds out = sdsempty();
    switch (r->type) {
    case SWARMKV_REPLY_ERROR:
        out = sdscatprintf(out,"(error) %s\n", r->str);
		break;
    case SWARMKV_REPLY_STATUS:
        out = sdscat(out, r->str);
        out = sdscat(out, "\n");
    	break;
    case SWARMKV_REPLY_INTEGER:
        out = sdscatprintf(out,"(integer) %lld\n", r->integer);
    	break;
	case SWARMKV_REPLY_DOUBLE:
		out = sdscatprintf(out,"(double) %f\n", r->dval);
		break;
    case SWARMKV_REPLY_STRING:
    case SWARMKV_REPLY_VERBATIM:
	case SWARMKV_REPLY_NODE:
        /* If you are producing output for the standard output we want
        * a more interesting output with quoted characters and so forth,
        * unless it's a verbatim string type. */
        if (r->type == SWARMKV_REPLY_STRING) {
            out = sdscatrepr(out, r->str, r->len);
            out = sdscat(out, "\n");
        } else {
            out = sdscatlen(out, r->str, r->len);
            out = sdscat(out, "\n");
        }
    	break;
    case SWARMKV_REPLY_NIL:
		out = sdscat(out, "(nil)\n");
		break;
    case SWARMKV_REPLY_ARRAY:
        if (r->n_element == 0)
		{
			out = sdscat(out,"(empty array)\n");
        } 
		else
		{
            unsigned int i, idxlen = 0;
            char _prefixlen[16];
            char _prefixfmt[16];
            sds _prefix;
            sds tmp;

            /* Calculate chars needed to represent the largest index */
            i = r->n_element;
            do
			{
                idxlen++;
                i /= 10;
            } while(i);

            /* Prefix for nested multi bulks should grow with idxlen+2 spaces */
            memset(_prefixlen,' ', idxlen+2);
            _prefixlen[idxlen+2] = '\0';
            _prefix = sdscat(sdsnew(prefix), _prefixlen);

            /* Setup prefix format for every entry */
            char numsep = ')';
            snprintf(_prefixfmt,sizeof(_prefixfmt), "%%s%%%ud%c ", idxlen, numsep);

            for (i = 0; i < r->n_element; i++) 
			{
                unsigned int human_idx = i;
                human_idx++; /* Make it 1-based. */

                /* Don't use the prefix for the first element, as the parent
                 * caller already prepended the index number. */
                out = sdscatprintf(out, _prefixfmt, i == 0 ? "" : prefix, human_idx);

                /* Format the multi bulk entry */
                tmp = swarmkv_reply_format(r->elements[i], _prefix);
                out = sdscatlen(out, tmp, sdslen(tmp));
                sdsfree(tmp);
            }
            sdsfree(_prefix);
        }
		break;
    default:
        sdscatprintf(out,"Unknown reply type: %d\n", r->type);
        break;
    }
    return out;
}
void swarmkv_reply_print(const struct swarmkv_reply *reply, FILE* stream)
{
	sds formated=swarmkv_reply_format(reply, "");
	fwrite(formated, sdslen(formated), 1, stream);
	sdsfree(formated);
	return;
}
struct swarmkv_reply *swarmkv_reply_new_string(const char* str, size_t sz)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STRING;
	reply->len=sz;
	reply->str=sdsnewlen(str, sz);
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_verbatim(const char *str, size_t sz, const char *ext)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_VERBATIM;
	reply->len=sz;
	reply->str=sdsnewlen(str, sz);
	for (int i = 0; i < 3; i++)
	{
		if (*ext == '\0') {
			reply->vtype[i] = ' ';
		} else {
			reply->vtype[i] = *ext++;
		}
	}
	return reply;
}

struct swarmkv_reply *swarmkv_reply_new_string_from_integer(long long integer)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STRING;
	reply->str=sdsfromlonglong(integer);
	reply->len=sdslen(reply->str);
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_string_fmt(const char *fmt, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STRING;
	va_list ap;
    va_start(ap, fmt);
	reply->str=sdscatvprintf(sdsempty(), fmt, ap);
    va_end(ap);
	reply->len=sdslen(reply->str);
	return reply;

}
struct swarmkv_reply *swarmkv_reply_new_array(size_t n_element)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ARRAY;
	reply->n_element=n_element;
	reply->elements=ALLOC(struct swarmkv_reply*, n_element);
	return reply;

}
struct swarmkv_reply *swarmkv_reply_new_integer(long long integer)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_INTEGER;
	reply->integer=integer;
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_double(double dval)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_DOUBLE;
	reply->dval=dval;
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_nil(void)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_NIL;
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_status(const char *format, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_STATUS;
	va_list ap;
    va_start(ap, format);
	reply->str=sdscatvprintf(sdsempty(), format, ap);
    va_end(ap);
	reply->len=strlen(reply->str);
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_error(const char *format, ...)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ERROR;
	va_list ap;
    va_start(ap, format);
	reply->str=sdscatvprintf(sdsempty(), format, ap);
    va_end(ap);
	reply->len=strlen(reply->str);
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_node(node_t *node, int is_ask)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_NODE;
	if(is_ask)
	{
		reply->str=sdscatfmt(sdsempty(), "-ASK %s", node->addr);
	}
	else
	{
		reply->str=sdscatfmt(sdsempty(), "%s", node->addr);
	}
	reply->len=strlen(reply->str);
	return reply;
}
int node_sanity(const node_t *node)
{
	char c=0;
	for(size_t i=0; i<sizeof(node->addr); i++)
	{
		c=node->addr[i];
		if((c>='0'&&c<='9') || 
			c=='.' ||
			c==':'||
			c==0)
		{
			continue;
		}
		else
		{
			return 0;
		}
	}
	return 1;
}
sds node_addr2sds(const node_t *node)
{
	return sdsnew(node->addr);
}
int node_init_from_sds(node_t *node, const char *addr_str)
{
	memset(node, 0, sizeof(node_t));
	strncpy(node->addr, addr_str, sizeof(node->addr));
	return 0;
}
int node_compare(const node_t *node1, const node_t *node2)
{
	assert(strchr(node1->addr, ':'));
	assert(strchr(node2->addr, ':'));
	assert(node_sanity(node1));
	assert(node_sanity(node2));
	return strcmp(node1->addr, node2->addr);
}
int node_list_exists(const node_t *list, size_t n_list, const node_t *addr)
{
	for(size_t i=0; i<n_list; i++)
	{
		if(0==node_compare(list+i, addr))
		{
			return 1;
		}
	}
	return 0;
}
int node_list_remove(node_t *list, size_t n_list, const node_t *addr)
{
	int removed=0;
	node_t copy[n_list];
	memcpy(copy, list, sizeof(node_t)*n_list);
	for(size_t i=0, j=0; i<n_list; i++)
	{
		if(0!=node_compare(copy+i, addr))
		{
			memcpy(list+j, copy+i, sizeof(node_t));
			j++;
		}
		else
		{
			removed++;
		}
	}
	return removed;
}
void node_copy(node_t *dst, const node_t *src)
{
    memset(dst, 0, sizeof(node_t));
	assert(node_sanity(src));
    memcpy(dst, src, sizeof(node_t));
	return;
}
int node_is_empty(const node_t *node)
{
	return ((node==NULL)||(node->addr[0]==0));
}
void node_init(node_t *node, const char *ipv4, unsigned int port)
{
	memset(node, 0, sizeof(node_t));
	snprintf(node->addr, sizeof(node->addr), "%s:%u", ipv4, port);
	return;
}
void node_init_from_string(node_t *node, const char *addr_str)
{
	memset(node, 0, sizeof(node_t));
	assert(strlen(addr_str)<sizeof(node_t));
	assert(strchr(addr_str, ':'));
	strncpy(node->addr, addr_str, sizeof(node->addr));
	return;
}
int node_init_from_reply(node_t *node, const struct swarmkv_reply *reply)
{
	if(reply->type!=SWARMKV_REPLY_NODE)
	{
		return -1;
	}
	memset(node, 0, sizeof(node_t));
	const char *p=reply->str;
	if(0==strncasecmp(reply->str, "-ASK ", 5))
	{
		p+=5;
	}
	node_init_from_string(node, p);
	return 0;
}
int node_list_new_from_reply(node_t **node_list, size_t *n_node, const struct swarmkv_reply *reply)
{
	if(reply->type==SWARMKV_REPLY_NIL)
	{
		*n_node=0;
		return 0;
	}
	if(reply->type!=SWARMKV_REPLY_ARRAY)
	{
		return -1;
	}
	*n_node=reply->n_element;
	*node_list=ALLOC(node_t, *n_node);
	for(size_t i=0; i<*n_node; i++)
	{
		node_init_from_reply(*node_list+i, reply->elements[i]);
	}
	return 0;
}
int node_parse(const node_t *node, unsigned int *port, char *addr_str, size_t sz_addr)
{
	int n_read=0;
	n_read=sscanf(node->addr, "%[^:]:%u", addr_str, port);
	return n_read==2?0:-1;
}
void node_init_from_sockaddr(node_t *node, const struct sockaddr * sa)
{
	char ip[MAX_IPV4_ADDR_LEN+1];
	size_t ip_len=sizeof(ip);
	unsigned int port=0;
    switch(sa->sa_family)
	{
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                    ip, ip_len);
			port=htons(((struct sockaddr_in *)sa)->sin_port);
            break;
        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                   ip, ip_len);
			port=htons(((struct sockaddr_in6 *)sa)->sin6_port);
			assert(0);//node_t cann't hold IPv6 yet.
            break;
        default:
			assert(0);
            break;;
    }
	memset(node, 0, sizeof(node_t));
	snprintf(node->addr, sizeof(node->addr), "%s:%u", ip, port);
	return;
}
void node_to_sockaddr(const node_t *node, struct sockaddr *sockaddr)
{
	struct sockaddr_in *sa4=(struct sockaddr_in *) sockaddr;
	struct sockaddr_in6 *sa6=(struct sockaddr_in6*) sockaddr;
	char ip[16]={};
	unsigned int port=0;
	memset(sockaddr, 0, sizeof(struct sockaddr));
	node_parse(node, &port, ip, sizeof(ip));
	if(NULL==strstr(ip, ":"))//ipv4
	{		
		sa4->sin_family = AF_INET;
		sa4->sin_addr.s_addr = inet_addr(ip);
		sa4->sin_port = htons(port);
	}
	else//ipv6
	{
		sa6->sin6_family = AF_INET6;
	    inet_pton(AF_INET6, ip, &sa6->sin6_addr);
	    sa6->sin6_port  = htons(port);
	}
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
		case SWARMKV_REPLY_NODE:
		case SWARMKV_REPLY_VERBATIM:
			copy->len=origin->len;
			copy->str=sdsdup(origin->str);
			if(origin->type==SWARMKV_REPLY_VERBATIM) memcpy(copy->vtype, origin->vtype, 4);
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
	if(src->type==SWARMKV_REPLY_NIL)
	{
		swarmkv_reply_free(src);
		return;
	}
	if(*dst==NULL)
	{
		*dst=swarmkv_reply_new_array(0);
	}
	if((*dst)->type!=SWARMKV_REPLY_ARRAY || src->type!=SWARMKV_REPLY_ARRAY)
	{
		return;
	}
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

void swarmkv_reply_append_string(struct swarmkv_reply **dst, struct swarmkv_reply *src)
{
	if(src->type==SWARMKV_REPLY_NIL) return;
	if(*dst==NULL)
	{
		*dst=swarmkv_reply_new_array(0);
	}
	if((*dst)->type!=SWARMKV_REPLY_ARRAY || src->type!=SWARMKV_REPLY_STRING)
	{
		return;
	}
	(*dst)->elements=(struct swarmkv_reply **)realloc((*dst)->elements, ((*dst)->n_element+1)*sizeof(struct swarmkv_reply*));
    (*dst)->elements[(*dst)->n_element]=src;
    (*dst)->n_element+=1;

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
		case SWARMKV_REPLY_NODE:
		case SWARMKV_REPLY_VERBATIM:
			sdsfree(reply->str);
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
struct swarmkv_cmd *swarmkv_cmd_new(size_t argc)
{
	size_t size=sizeof(struct swarmkv_cmd)+argc*sizeof(sds);
	struct swarmkv_cmd *cmd=(struct swarmkv_cmd*)malloc(size);
	cmd->argc=argc;
	cmd->argv=(sds*)((char*)cmd+sizeof(struct swarmkv_cmd));
	return cmd;
}
void swarmkv_cmd_free(struct swarmkv_cmd *p)
{
	size_t i=0;
	for(i=0; i<p->argc; i++)
	{
		sdsfree(p->argv[i]);
		p->argv[i]=NULL;
	}
	p->argv=NULL;
	free(p);
}
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd *origin)
{
	struct swarmkv_cmd* copy=swarmkv_cmd_new(origin->argc);
	size_t i=0;
	for(i=0; i<origin->argc; i++)
	{
		copy->argv[i]=sdsdup(origin->argv[i]);
	}
	return copy;
}
sds keyslots2json(void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container)
{
	cJSON *slot_array=cJSON_CreateArray();
	cJSON *slot=NULL;
	char range[256]="";
	size_t i=0;
	sds json_sds=NULL;
	assert(KEYSPACE_SLOT_NUM==n_slot_container);
	assert(sz_slot_container>=sizeof(struct key_slot));
	struct key_slot *iterator=(struct key_slot *)(slot_container_base+offset_slot);
	struct key_slot *start_slot=NULL, *end_slot=NULL;
	start_slot=end_slot=iterator;
	for(i=0; i<n_slot_container+1; i++)
	{
		iterator=(struct key_slot *)(slot_container_base+sz_slot_container*i+offset_slot);
		if(i<n_slot_container
			&& 0==node_compare(&(iterator->owner), &(end_slot->owner)))
		{
			end_slot=iterator;
			continue;
		}	
		slot=cJSON_CreateObject();
		if(start_slot==end_slot)
		{
			snprintf(range, sizeof(range), "%d", start_slot->slot_id);
		}
		else
		{
			snprintf(range, sizeof(range), "%d-%d", start_slot->slot_id, end_slot->slot_id);
		}
		cJSON_AddStringToObject(slot, "slot", range);
		cJSON_AddStringToObject(slot, "owner", end_slot->owner.addr);
		cJSON_AddItemToArray(slot_array, slot);
		start_slot=end_slot=iterator;
	}
	char* payload=NULL;
	payload=cJSON_Print(slot_array);
	cJSON_Delete(slot_array);
	json_sds=sdsnew(payload);
	free(payload);
	return json_sds;
}
void json2keyslots(sds json_buffer, void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container)
{
	cJSON *slot_table=NULL, *range=NULL, *slot=NULL, *owner=NULL;
	int	i=0, j=0, k=0, n_record=0;
	int range_start=0, range_end=0;
	slot_table=cJSON_Parse(json_buffer);
	n_record=cJSON_GetArraySize(slot_table);
	assert(n_slot_container==KEYSPACE_SLOT_NUM);
	struct key_slot *iterator=NULL;
	for(i=0; i<n_record; i++)
	{
		slot=cJSON_GetArrayItem(slot_table, i);
		range=cJSON_GetObjectItem(slot, "slot");
		if(strstr(range->valuestring, "-"))
		{
			sscanf(range->valuestring, "%d-%d", &range_start, &range_end);
		}
		else
		{
			sscanf(range->valuestring, "%d", &range_start);
			range_end=range_start;
		}
		owner=cJSON_GetObjectItem(slot, "owner");
		for(j=range_start; j<range_end+1; j++)
		{
			iterator=(struct key_slot*)(slot_container_base+sz_slot_container*j+offset_slot);
			iterator->slot_id=j;
			node_init_from_string(&iterator->owner, owner->valuestring);
			k++;
		}
	}
	assert(k==KEYSPACE_SLOT_NUM);
	cJSON_Delete(slot_table);
	return;
}
void health_response2active_nodes(const char *resp_buff, node_t *nodes, uuid_t *uuids, size_t *n_node)
{
	size_t n_record=0, i=0;
	cJSON *health_reponse=NULL, *record=NULL, *service=NULL;
	cJSON *ip=NULL, *port=NULL, *meta=NULL, *meta_node_uuid=NULL;
	health_reponse=cJSON_Parse(resp_buff);
	n_record=cJSON_GetArraySize(health_reponse);
	for(i=0; i<n_record && i<*n_node; i++)
	{
		record=cJSON_GetArrayItem(health_reponse, i);
		service=cJSON_GetObjectItem(record, "Service");
		ip=cJSON_GetObjectItem(service, "Address");
		port=cJSON_GetObjectItem(service, "Port");
		node_init(nodes+i, ip->valuestring, (unsigned int)port->valueint);
		if(uuids)
		{
			meta=cJSON_GetObjectItem(service, "Meta");
			meta_node_uuid=cJSON_GetObjectItem(meta, "node_uuid");
			uuid_parse(meta_node_uuid->valuestring, uuids[i]); 
		}
	}
	cJSON_Delete(health_reponse);
	*n_node=i;
}
sds node_print_json(const node_t *self, uuid_t uuid)
{
	cJSON* node_info=NULL;
	char uuid_str[MAX_UUID_STR_LEN];
	uuid_unparse_lower(uuid, uuid_str);
	node_info=cJSON_CreateObject();

	cJSON_AddStringToObject(node_info, "node", self->addr);
	cJSON_AddStringToObject(node_info, "node_uuid", uuid_str);
	char* payload=NULL;
	payload=cJSON_Print(node_info);
	cJSON_Delete(node_info);
	node_info=NULL;
	
	sds self_json=NULL;
	self_json=sdsnew(payload);
	free(payload);
	return self_json;
}
void leader_response2leader_node(const char *resp_body, node_t *leader)
{
	cJSON *root=NULL;
	cJSON *item=NULL;
	root=cJSON_Parse(resp_body);
	item=cJSON_GetObjectItem(root, "node");
	node_init_from_string(leader, item->valuestring);;
	cJSON_Delete(root);
}
/* This is an modified version of keyHashSlot of Redis source code.
 * If the key contains the {...} pattern, only the part between
 * { and } is hashed. This is to force certain keys to be in
 * the same node. 
*/
#define	KEYSPACE_XXHASH_SEED	5210
int key_hash_slot(const char *key, size_t keylen)
{
	int slot_id=-1;
    int s=0, e=0; /* start-end indexes of { and } */

    for (s = 0; s < keylen; s++)
        if (key[s] == '{') break;

    /* No '{' ? Hash the whole key. This is the base case. */
    if (s == keylen)
	{
		slot_id=XXH32(key, keylen, KEYSPACE_XXHASH_SEED)%KEYSPACE_SLOT_NUM;
		return slot_id;
	}

    /* '{' found? Check if we have the corresponding '}'. */
    for (e = s+1; e < keylen; e++)
        if (key[e] == '}') break;

    /* No '}' or nothing between {} ? Hash the whole key. */
    if (e == keylen || e == s+1)
	{
		slot_id=XXH32(key, keylen, KEYSPACE_XXHASH_SEED)%KEYSPACE_SLOT_NUM;
		return slot_id;
	}
	return XXH32(key+s+1, e-s-1, KEYSPACE_XXHASH_SEED)%KEYSPACE_SLOT_NUM;
}
int key2tid(const sds key, int nr_worker_threads)
{
	return key_hash_slot(key, sdslen(key))%nr_worker_threads;
}
struct http_client
{
	int response_code;
	struct event_base* evbase;
	struct evhttp_connection* evhttpconn;
	struct evhttp_request *req;	
	int error_happens;
	sds response_body;
};
void __http_blocking_request_cb(struct evhttp_request *req, void *arg)
{	
	struct http_client* client = (struct http_client*) arg;
	if(!req || !evhttp_request_get_response_code(req))
	{
		client->error_happens=1;
	}
	else
	{		
		client->response_code=evhttp_request_get_response_code(req);
		if(client->response_code==200)
		{
			struct evbuffer *buf=NULL;
			buf=evhttp_request_get_input_buffer(client->req);
			size_t len = evbuffer_get_length(buf);
			client->response_body=sdsnewlen(SDS_NOINIT, len);
			evbuffer_remove(buf, client->response_body, len);
		}
	}
	event_base_loopbreak(client->evbase);
}
int http_blocking_request(enum evhttp_cmd_type type, const char *host, unsigned int port, const char *url, const sds req_body, sds *resp_body)
{
	struct http_client client_data;
	struct http_client* client=&client_data;
	memset(&client_data, 0, sizeof(client_data));
	client->evbase=event_base_new();	//temporary evbase for blocking query
    client->evhttpconn = evhttp_connection_base_new(client->evbase, NULL, host, port);
    client->req = evhttp_request_new(__http_blocking_request_cb, client);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(client->req);
	char hostname[256]="";
	snprintf(hostname, sizeof(hostname), "%s:%u", host, port);
	evhttp_add_header(output_headers, "Host", hostname);
	evhttp_add_header(output_headers, "Connection", "close");
	if(req_body)
	{
		struct evbuffer* output_buffer = evhttp_request_get_output_buffer(client->req);
		evbuffer_add(output_buffer, req_body, sdslen(req_body));
		char number[64];
		snprintf(number, sizeof(number), "%zu", sdslen(req_body));
		evhttp_add_header(output_headers, "Content-Type", "application/json");
		evhttp_add_header(output_headers, "Content-Length", number);
	}
    evhttp_make_request(client->evhttpconn, client->req, type, url);
    evhttp_connection_set_timeout(client->evhttpconn, 5);
    event_base_dispatch(client->evbase);
	if(client->response_code == 200)
	{
		if(resp_body)
		{
			*resp_body=client->response_body;
		}
		else
		{
			sdsfree(client->response_body);			
		}	
		client->response_body=NULL;
	}
//	evhttp_request_free(client->req);
	evhttp_connection_free(client->evhttpconn);
	event_base_free(client->evbase);
	return client->response_code;	
}
//Source: https://stackoverflow.com/a/779960
char *str_replace(char *orig, char *rep, char *with) 
{
    char *result; // the return string
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}
