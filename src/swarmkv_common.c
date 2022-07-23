#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"

#include "uthash.h"
#include "xxhash.h"
#include "cJSON.h"

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
    va_start(ap, format);
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
    va_start(ap, format);
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
    va_start(ap, format);
	reply->len=vasprintf(&(reply->str), format, ap);
    va_end(ap);
	
	return reply;
}
struct swarmkv_reply *swarmkv_reply_new_node(struct swarmkv_node_addr *addr, int is_ask)
{
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_NODE;
	char node_str[256]={0};
	
	if(is_ask)
	{
		snprintf(node_str, sizeof(node_str), "-ASK %s:%u", addr->ip_addr, addr->p2p_port);
	}
	else
	{		
		snprintf(node_str, sizeof(node_str), "-ASK %s:%u", addr->ip_addr, addr->p2p_port);
	}
	reply->len=strlen(node_str)+1;
	reply->str=ALLOC(char, reply->len);
	memcpy(reply->str, node_str, strlen(node_str));
	return reply;
}
int swarmkv_reply2node_addr(const struct swarmkv_reply *reply, struct swarmkv_node_addr *node)
{
	if(reply->type!=SWARMKV_REPLY_NODE)
	{
		return -1;
	}
	memset(node, 0, sizeof(struct swarmkv_node_addr));
	const char *p=reply->str;
	if(0==strncasecmp(reply->str, "-ASK ", 5))
	{
		p+=5;
	}
	int n_read=0;
	n_read=sscanf(p, "%[^:]:%u", node->ip_addr, &node->p2p_port);
	if(n_read!=2)
	{
		return -1;
	}
	return 0;
}
int swarmkv_reply2nodelist(const struct swarmkv_reply *reply, struct swarmkv_node_addr **node_list, size_t *n_node)
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
	*node_list=ALLOC(struct swarmkv_node_addr, *n_node);
	for(size_t i=0; i<*n_node; i++)
	{
		swarmkv_reply2node_addr(reply->elements[i], *node_list+i);
	}
	return 0;
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
int swarmkv_node_addr_compare(const struct swarmkv_node_addr *addr1, const struct swarmkv_node_addr *addr2)
{
	int ret=0;
	ret=strcmp(addr1->ip_addr, addr2->ip_addr);
	if(ret==0)
	{
		ret=addr1->p2p_port - addr2->p2p_port;
	}
	return ret;
}
int swarmkv_node_addr_list_exists(const struct swarmkv_node_addr *list, size_t n_list, const struct swarmkv_node_addr *addr)
{
	for(size_t i=0; i<n_list; i++)
	{
		if(0==swarmkv_node_addr_compare(list+i, addr))
		{
			return 1;
		}
	}
	return 0;
}
int swarmkv_node_addr_list_remove(struct swarmkv_node_addr *list, size_t n_list, const struct swarmkv_node_addr *addr)
{
	int removed=0;
	for(size_t i=0; i<n_list; i++)
	{
		if(0==swarmkv_node_addr_compare(list+i, addr))
		{
			if(i<n_list-1)
			{
				memmove(list+i, list+i+1, sizeof(struct swarmkv_node_addr)*(n_list-i-1));
				i--;
			}			
			removed++;
		}
	}
	return removed;
}
void swarmkv_node_addr_copy(struct swarmkv_node_addr *dst, const struct swarmkv_node_addr *src)
{
	memcpy(dst, src, sizeof(struct swarmkv_node_addr));
	return;
}
int swarmkv_node_compare(const struct swarmkv_node *node1, const struct swarmkv_node *node2)
{
	int ret=0;
	ret=swarmkv_node_addr_compare(&node1->addr, &node2->addr);
	if(ret==0)
	{
		ret=uuid_compare(node1->bin_uuid, node2->bin_uuid);
	}
	return ret;
}
void swarmkv_node_copy(struct swarmkv_node *dst, const struct swarmkv_node *src)
{
	memcpy(dst, src, sizeof(struct swarmkv_node));
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
		case SWARMKV_REPLY_NODE:
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

void swarmkv_reply_free(struct swarmkv_reply *reply)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_NODE:
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
sds keyslots2json(void *slot_container_base, size_t sz_slot_container, size_t offset_slot, size_t n_slot_container)
{
	cJSON *slot_array=cJSON_CreateArray();
	cJSON *slot=NULL, *node=NULL;
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
			&& 0==swarmkv_node_compare(&(iterator->owner), &(end_slot->owner)))
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
		node=cJSON_CreateObject();
		cJSON_AddStringToObject(node, "ip", end_slot->owner.addr.ip_addr);
		cJSON_AddNumberToObject(node, "p2p_port", end_slot->owner.addr.p2p_port);
		cJSON_AddItemToObject(slot, "owner", node);
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
	cJSON *slot_table=NULL, *range=NULL, *slot=NULL, *owner=NULL, *ip=NULL, *p2p_port=NULL, *node_uuid=NULL;
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
		ip=cJSON_GetObjectItem(owner, "ip");	
		p2p_port=cJSON_GetObjectItem(owner, "p2p_port");
		node_uuid=cJSON_GetObjectItem(owner, "node_uuid");
		for(j=range_start; j<range_end+1; j++)
		{
			iterator=(struct key_slot*)(slot_container_base+sz_slot_container*j+offset_slot);
			iterator->slot_id=j;
			strncpy(iterator->owner.addr.ip_addr, ip->valuestring, sizeof(iterator->owner.addr.ip_addr));
			iterator->owner.addr.p2p_port=(unsigned int)p2p_port->valueint;
			if(node_uuid)
				uuid_parse(node_uuid->valuestring, iterator->owner.bin_uuid);
			k++;
		}
	}
	assert(k==KEYSPACE_SLOT_NUM);
	cJSON_Delete(slot_table);
	return;
}
void json2nodes(const char* resp_buff, struct swarmkv_node *nodes, size_t *n_node)
{
	size_t n_record=0, i=0;
	cJSON *health_reponse=NULL, *record=NULL, *item=NULL, *service=NULL, *meta=NULL, *meta_node_uuid=NULL;

	health_reponse=cJSON_Parse(resp_buff);
	n_record=cJSON_GetArraySize(health_reponse);
	for(i=0; i<n_record && i<*n_node; i++)
	{
		record=cJSON_GetArrayItem(health_reponse, i);
		service=cJSON_GetObjectItem(record, "Service");
		item=cJSON_GetObjectItem(service, "Address");
		strncpy(nodes[i].addr.ip_addr, item->valuestring, sizeof(nodes[i].addr.ip_addr));
		item=cJSON_GetObjectItem(service, "Port");
		nodes[i].addr.p2p_port=(unsigned int)item->valueint;
		meta=cJSON_GetObjectItem(service, "Meta");
		meta_node_uuid=cJSON_GetObjectItem(meta, "node_uuid");
		uuid_parse(meta_node_uuid->valuestring, nodes[i].bin_uuid); 
	}
	cJSON_Delete(health_reponse);
	*n_node=i;
}
void json2leader(const char *resp_body, struct swarmkv_node *leader)
{
	cJSON *leader_json=NULL, *item=NULL;
	leader_json=cJSON_Parse(resp_body);
	item=cJSON_GetObjectItem(leader_json, "ip");
	strncpy(leader->addr.ip_addr, item->valuestring, sizeof(leader->addr.ip_addr));
	item=cJSON_GetObjectItem(leader_json, "p2p_port");
	leader->addr.p2p_port=(unsigned int)item->valueint;
	item=cJSON_GetObjectItem(leader_json, "node_uuid");
	uuid_parse(item->valuestring, leader->bin_uuid);
}
#define	KEYSPACE_XXHASH_SEED	5210
int get_slot_id(const char* key, size_t keylen)
{
	XXH32_hash_t hash = XXH32(key, keylen, KEYSPACE_XXHASH_SEED);
	int slot_id=hash%KEYSPACE_SLOT_NUM;
	return slot_id;
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
int http_blocking_request(enum evhttp_cmd_type type, const char* host, unsigned int port, const char* url, const sds req_body, sds *resp_body)
{
	struct http_client client_data;
	struct http_client* client=&client_data;
	memset(&client_data, 0, sizeof(client_data));
	client->evbase=event_base_new();	//temporary evbase for blocking query
    client->evhttpconn = evhttp_connection_base_new(client->evbase, NULL, host, port);
    client->req = evhttp_request_new(__http_blocking_request_cb, client);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(client->req);
	char hostname[256]="";
	snprintf(hostname, sizeof(hostname), "%s:%d", host, port);
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


