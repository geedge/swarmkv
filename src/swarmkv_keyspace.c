#include "swarmkv_keyspace.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include "swarmkv_net.h"

#include "cJSON.h"
#include "sds.h"
#include "base64.h"
#include "xxhash.h"
#include "uthash.h"
#include "utlist.h"

#include <MESA/MESA_handle_logger.h>

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/prctl.h>

#define	CONSUL_HOST	"127.0.0.1"
#define	CONSUL_DEFAULT_PORT	8500

#define	LOCAL_HOST	"127.0.0.1"

#define	KEYSPACE_XXHASH_SEED	5210

#define MODULE_SWARMKV_KEYSPACE		module_name_str("swarmkv.keyspace")
const char *error_cluster_leader_not_found="ERR cluster leader not found";
const char *error_list_health_node_failed="ERR list health node failed";
const char *error_read_slot_table_failed="ERR read slot table failed";

enum slot_state
{
	STATE_STABLE=0,
	STATE_MIGRATING,
	STATE_IMPORTING
};

struct key_slot
{
	int slot_id;
	enum slot_state state;
	struct swarmkv_node owner;
	struct swarmkv_node peer;//peer is used for state MIGRATING and IMPORTING
//	struct swarmkv_node_addr replica[MAX_REPLICA_NUM];
	struct key_route_entry* keyroute_table;
	int I_am_owner;
	pthread_mutex_t mutex;
};

struct key_route_entry
{
	sds key;
	struct swarmkv_node_addr owner;
	UT_hash_handle hh;
};
struct key_route_entry *key_entry_new(const sds key, const struct swarmkv_node_addr* owner)
{
	struct key_route_entry *entry=NULL;
	entry=ALLOC(struct key_route_entry, 1);
	entry->key=sdsdup(key);
	memcpy(&(entry->owner), owner, sizeof(struct swarmkv_node_addr));
	return entry;
}

void key_entry_free(struct key_route_entry *entry)
{
	sdsfree(entry->key);
	entry->key=NULL;
	free(entry);
}
static inline int get_slot_id(const char* key, size_t keylen)
{
	XXH32_hash_t hash = XXH32(key, keylen, KEYSPACE_XXHASH_SEED);
	int slot_id=hash%SWARMKV_SLOT_NUM;
	return slot_id;
}

struct swarmkv_keyspace
{
	char db_name[SWARMKV_SYMBOL_MAX];
	pthread_t thr;
	int readable_tid;

	struct swarmkv_node self;
	pthread_rwlock_t rwlock;	
	struct key_slot slots[SWARMKV_SLOT_NUM];

	unsigned int consul_agent_port;
	unsigned int health_check_port;	//HTTP
	int dryrun;
	int init_success;

	int is_leader;
	int consul_lead_key_modify_idx;
	int consul_slots_modify_idx;
	int consul_nodes_modify_idx;
	char consul_service_id[SWARMKV_SYMBOL_MAX];
	char consul_check_id[SWARMKV_SYMBOL_MAX];
	char consul_session_id[SWARMKV_SYMBOL_MAX];

	struct evhttp* http_server;
	struct event_base * evbase;
	
	//reference, no need free
	void *logger;
};
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks);
void consul_watch_leadership_changes_async(struct swarmkv_keyspace* ks);
void consul_watch_nodes_changes_async(struct swarmkv_keyspace* ks);
void consul_watch_slots_changes_async(struct swarmkv_keyspace* ks);

void health_check_cb(struct evhttp_request *request, void *arg)
{
	struct evbuffer* evbuf_reply = evbuffer_new();
	 struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);

	 //HTTP header
	evhttp_add_header(output_headers, "Server", "swarmkv v2.0");
	evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=UTF-8");//utf8
	evhttp_add_header(output_headers, "Connection", "close");
	
	evbuffer_add_printf(evbuf_reply, "SwarmKV Cluster");
	evhttp_send_reply(request, HTTP_OK, "OK", evbuf_reply);
	return;
}

struct evhttp* http_server_new(struct event_base* evbase, unsigned port_listen, void* logger)
{
    struct evhttp* http = evhttp_new(evbase);

    if (evhttp_bind_socket(http, "0.0.0.0", port_listen) != 0)
    {
		MESA_handle_runtime_log(logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "evhttp_bind_socket on port %u failed.", port_listen);		
		return NULL;
    }
    int http_option_timeout = 5; //in seconds
    evhttp_set_timeout(http, http_option_timeout);
    evhttp_set_allowed_methods( http , EVHTTP_REQ_GET);
    //Set a callback for a specified URI
    evhttp_set_cb(http, "/health", health_check_cb, NULL);
	return http;
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
			len=evbuffer_remove(buf, client->response_body, len);
		}
	}
	event_base_loopbreak(client->evbase);
}
int http_blocking_request(enum evhttp_cmd_type type, const char* host, unsigned int port, const char* url, sds req_body, sds* resp_body)
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
//	evhttp_connection_free(client->evhttpconn);
	event_base_free(client->evbase);
	return client->response_code;	
}


/*
int consul_get_self_ip(struct swarmkv_keyspace* ks)
{
	sds response=NULL;
	int resp_code=0, ret=-1;
	cJSON *self=NULL, *member=NULL, *item=NULL;
	resp_code=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, "/v1/agent/self", NULL, &response);
	if(resp_code == 200)
	{
		self=cJSON_Parse(response);
		member=cJSON_GetObjectItem(self, "Member");
		item=cJSON_GetObjectItem(member, "Addr");
		strncpy(ks->self.ip_addr, item->valuestring, sizeof(ks->self_ip));
		ret=0;
		cJSON_Delete(self);
	}
	//cjson parse
	sdsfree(response); 	
	response=NULL;
	return ret;
}
*/
int consul_service_register(struct swarmkv_keyspace* ks)
{
	int ret=0;
	char health_check_url[SWARMKV_URL_MAX]="";
	char uuid_str[37];
	cJSON* service=cJSON_CreateObject();
	cJSON_AddStringToObject(service, "Name", ks->db_name);
	snprintf(ks->consul_service_id, sizeof(ks->consul_service_id), "%s-%d", ks->db_name, ks->health_check_port);
	cJSON_AddStringToObject(service, "ID", ks->consul_service_id);
	cJSON_AddStringToObject(service, "Address", ks->self.addr.ip_addr);
	cJSON_AddNumberToObject(service, "Port", ks->self.addr.p2p_port);

	cJSON* meta=cJSON_CreateObject();
	uuid_unparse_lower(ks->self.bin_uuid, uuid_str);
	cJSON_AddStringToObject(meta, "node_uuid", uuid_str);
	
	cJSON_AddItemToObject(service, "Meta", meta);

	cJSON* check=cJSON_CreateObject();

	cJSON_AddStringToObject(check, "Name", ks->db_name);
	snprintf(ks->consul_check_id, sizeof(ks->consul_check_id), "%s-%d", ks->db_name, ks->health_check_port);
	cJSON_AddStringToObject(check, "CheckID", ks->consul_check_id);
	snprintf(health_check_url, sizeof(health_check_url), "http://localhost:%u/health", ks->health_check_port);
	cJSON_AddStringToObject(check, "HTTP", health_check_url);
	cJSON_AddStringToObject(check, "Interval", "2s");
	cJSON_AddStringToObject(check, "Timeout", "1s");
	cJSON_AddStringToObject(check, "DeregisterCriticalServiceAfter", "5m");
	cJSON_AddItemToObject(service, "Check", check);

	char* payload=NULL;
	payload=cJSON_Print(service);
	sds req_body=sdsnew(payload);
	//printf("%s\n", req_body);
	free(payload);
	
	sds resp_body=NULL;
	int resp_code=0;
	resp_code=http_blocking_request(EVHTTP_REQ_PUT, CONSUL_HOST, ks->consul_agent_port, "/v1/agent/service/register?replace-existing-checks=true", req_body, &resp_body);
	if(resp_code!=200)
	{
		ret=-1;
	}
	sdsfree(req_body);
	sdsfree(resp_body);
	cJSON_Delete(service);
	return ret;
}


void __consul_session_create_cb(struct evhttp_request *req, void *arg)
{
	int resp_code=0;
	cJSON* session_id=NULL, *session_create_response=NULL;
	struct swarmkv_keyspace* ks = (struct swarmkv_keyspace *)arg;
	int is_running_for_leader=0;
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "session create failed: socket error %s",
			evutil_socket_error_to_string(errcode));	
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
		
	}
	if(resp_code==200)
	{
		struct evbuffer *buf=NULL;
		buf=evhttp_request_get_input_buffer(req);
		size_t len = evbuffer_get_length(buf);
		sds out=sdsnewlen(SDS_NOINIT, len);
		evbuffer_copyout(buf, out, len);
		
		session_create_response=cJSON_Parse(out);
		session_id=cJSON_GetObjectItem(session_create_response, "ID");
		strncpy(ks->consul_session_id, session_id->valuestring, sizeof(ks->consul_session_id));
		cJSON_Delete(session_create_response);
		consul_acquire_session_lock_async(ks);
		sdsfree(out);
		is_running_for_leader=1;
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "session %s is created.", ks->consul_session_id);
	}
	else
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "session creation failed: HTTP code %d.",
			resp_code);	
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
	return;
}
static void __ks_dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
	return;
}

void *swarmkv_keyspace_thread(void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv-ks");
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);

	struct event * ev = event_new(ks->evbase, -1, EV_PERSIST, __ks_dummy_event_handler, NULL);
	struct timeval timer_delay = {2, 0};
	evtimer_add(ev, &timer_delay);

	event_base_dispatch(ks->evbase);
	return NULL;
}

void consul_create_session_async(struct swarmkv_keyspace* ks)
{
	cJSON* session=NULL, *service_check_array=NULL, *service_check=NULL;
	session=cJSON_CreateObject();
	cJSON_AddStringToObject(session, "LockDelay", "5s");
	cJSON_AddStringToObject(session, "Name", "swarmkv-leader-election-lock");
	cJSON_AddStringToObject(session, "Behavior", "release");

	service_check=cJSON_CreateObject();
	cJSON_AddStringToObject(service_check, "ID", ks->consul_check_id);
	service_check_array=cJSON_CreateArray();
	cJSON_AddItemToArray(service_check_array, service_check);
	cJSON_AddItemToObject(session, "ServiceChecks", service_check_array);

	char* payload=NULL;
	payload=cJSON_Print(session);
	sds req_body=sdsnew(payload);
	free(payload);
//	printf("%s\n", req_body);

	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/create");

	struct evhttp_request* request=evhttp_request_new(__consul_session_create_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	evhttp_connection_set_timeout(conn, 2);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");
	
	struct evbuffer* output_buffer = evhttp_request_get_output_buffer(request);
	evbuffer_add(output_buffer, req_body, sdslen(req_body));
	char number[64];
	snprintf(number, sizeof(number), "%zu", sdslen(req_body));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	evhttp_make_request(conn, request, EVHTTP_REQ_PUT, url);
	return;

}
void __consul_session_check_cb(struct evhttp_request *req, void *arg)
{
	int resp_code=0;
	cJSON *session_check=NULL;
	int session_check_array_size=0;
	struct swarmkv_keyspace* ks = (struct swarmkv_keyspace *)arg;
	int is_running_for_leader=0;
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "session check failed: socket error %s",
			evutil_socket_error_to_string(errcode));	
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
		
	}
	if(resp_code==200)
	{
		struct evbuffer *buf=NULL;
		buf=evhttp_request_get_input_buffer(req);
		size_t len = evbuffer_get_length(buf);
		sds out=sdsnewlen(SDS_NOINIT, len);
		evbuffer_copyout(buf, out, len);
		
		session_check=cJSON_Parse(out);
		session_check_array_size=cJSON_GetArraySize(session_check);
		cJSON_Delete(session_check);
		sdsfree(out);
		if(session_check_array_size==0)
		{
			consul_create_session_async(ks);		
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "session %s is not existed, start to create a new session.", ks->consul_session_id);
			memset(ks->consul_session_id, 0, sizeof(ks->consul_session_id));
		}
		else
		{
			consul_acquire_session_lock_async(ks);			
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "session %s is valid, run for leader.", ks->consul_session_id);
		}
		is_running_for_leader=1;
	}
	else
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "session check failed: HTTP code %d.",
			resp_code);	
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
	return;
}

void consul_session_check_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/info/%s", ks->consul_session_id);

	struct evhttp_request* request=evhttp_request_new(__consul_session_check_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	evhttp_connection_set_timeout(conn, 2);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");
	
	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	return;

}
void __acquire_session_lock_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	int resp_code=0;
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "run for leader failed: %s",
			evutil_socket_error_to_string(errcode));
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
		if(resp_code==200)
		{
			struct evbuffer *buf=NULL;
			buf=evhttp_request_get_input_buffer(req);
			size_t len = evbuffer_get_length(buf);
			sds resp_body_buff=sdsnewlen(SDS_NOINIT, len);
			evbuffer_copyout(buf, resp_body_buff, len);

			if(0==strncmp(resp_body_buff, "true", strlen("true")))
			{
				ks->is_leader=1;			
				MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "become cluster leader.");
				consul_watch_nodes_changes_async(ks);
			}
			else
			{			
				MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "lose the election.");
				ks->is_leader=0;
			}
			sdsfree(resp_body_buff);
		}
		else
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "run for leader failed: HTTP Code %d.", resp_code);
		}
	}
	consul_watch_leadership_changes_async(ks);
}
sds node_print_json(const struct swarmkv_node *self)
{
	cJSON* node_info=NULL;
	char uuid_str[MAX_UUID_STR_LEN];
	uuid_unparse_lower(self->bin_uuid, uuid_str);
	node_info=cJSON_CreateObject();
	
	cJSON_AddStringToObject(node_info, "ip", self->addr.ip_addr);
	cJSON_AddNumberToObject(node_info, "p2p_port", self->addr.p2p_port);
	cJSON_AddStringToObject(node_info, "node_uuid", uuid_str);
	char* payload=NULL;
	payload=cJSON_Print(node_info);

	sds self_json=NULL;
	self_json=sdsnew(payload);
	free(payload);
	return self_json;
}
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks)
{
	sds req_body=node_print_json(&(ks->self));

	char url[SWARMKV_URL_MAX];
	
	//run for leader
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?acquire=%s", ks->db_name, ks->consul_session_id);

	struct evhttp_request* request=evhttp_request_new(__acquire_session_lock_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	evhttp_connection_set_timeout(conn, 2);

	struct evbuffer* output_buffer = evhttp_request_get_output_buffer(request);
	evbuffer_add(output_buffer, req_body, sdslen(req_body));


	char number[64];
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	snprintf(number, sizeof(number), "%zu", sdslen(req_body));
	
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	evhttp_make_request(conn, request, EVHTTP_REQ_PUT, url);
	sdsfree(req_body);

}
void consul_run_for_leader_async(struct swarmkv_keyspace* ks)
{
	if(0==strlen(ks->consul_session_id))
	{
		consul_create_session_async(ks);
	}
	else
	{
		consul_session_check_async(ks);
	}
	return;
}
void __watch_leadership_changes_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	cJSON *metadata_array=NULL, *metadata=NULL, *modify_idx=NULL, *session=NULL;
	int resp_code=0;
	int is_running_for_leader=0;

	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: %s",
			evutil_socket_error_to_string(errcode));		
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
		if(resp_code==200)
		{
			struct evbuffer *buf=NULL;
			buf=evhttp_request_get_input_buffer(req);
			size_t len = evbuffer_get_length(buf);
			sds resp_body_buff=sdsnewlen(SDS_NOINIT, len);
			evbuffer_copyout(buf, resp_body_buff, len);
			metadata_array=cJSON_Parse(resp_body_buff);
			metadata=cJSON_GetArrayItem(metadata_array, 0);
			modify_idx=cJSON_GetObjectItem(metadata, "ModifyIndex");
			ks->consul_lead_key_modify_idx=modify_idx->valueint;
			session=cJSON_GetObjectItem(metadata, "Session");
			if(!session || 0==strlen(session->valuestring))
			{
				MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "cluster losts leader, start to run for leader.");
				consul_run_for_leader_async(ks);
				is_running_for_leader=1;
			}
			if(!session || 0!=strcmp(session->valuestring, ks->consul_session_id))
			{
				if(ks->is_leader)
				{
					MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "node is no longer a leader.");
					ks->is_leader=0;
				}
			}
			cJSON_free(metadata_array);
			sdsfree(resp_body_buff);
		}
		else if(resp_code==404)
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "no leader key, start to run for leader.");
			consul_run_for_leader_async(ks);
			is_running_for_leader=1;
		}
		else
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: HTTP code %d.", resp_code);
		}
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
}
void consul_watch_leadership_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	struct evhttp_request* request=evhttp_request_new(__watch_leadership_changes_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	evhttp_connection_set_timeout(conn, 1800);
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?index=%d&wait=10s", ks->db_name, ks->consul_lead_key_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	return;
}
void __watch_slots_changes_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	cJSON *metadata_array=NULL, *metadata=NULL, *value=NULL, *modify_idx=NULL;
	cJSON *slot_table=NULL, *range=NULL, *slot=NULL, *owner=NULL, *ip=NULL, *p2p_port=NULL, *node_uuid=NULL;
	int resp_code=0;
	int	i=0, j=0, n_record=0, n_slot=0;
	int range_start=0, range_end=0;
	struct swarmkv_node new_slot_owners[SWARMKV_SLOT_NUM];
	memset(new_slot_owners, 0, sizeof(new_slot_owners));
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "watch slots changes failed: %s",
			evutil_socket_error_to_string(errcode));		

	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
	}
	if(resp_code==200)
	{
		struct evbuffer *buf=NULL;
		buf=evhttp_request_get_input_buffer(req);
		size_t len = evbuffer_get_length(buf);
		sds resp_body_buff=sdsnewlen(NULL, len);
		evbuffer_copyout(buf, resp_body_buff, len);
		metadata_array=cJSON_Parse(resp_body_buff);
		metadata=cJSON_GetArrayItem(metadata_array, 0);
		modify_idx=cJSON_GetObjectItem(metadata, "ModifyIndex");
		ks->consul_slots_modify_idx=modify_idx->valueint;
		value=cJSON_GetObjectItem(metadata, "Value");
		sds decode_buffer=sdsnewlen(NULL, BASE64_DECODE_OUT_SIZE(strlen(value->valuestring)));
		base64_decode(value->valuestring, strlen(value->valuestring), (unsigned char*)decode_buffer);
		
		slot_table=cJSON_Parse(decode_buffer);
		n_record=cJSON_GetArraySize(slot_table);
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
				strncpy(new_slot_owners[n_slot].addr.ip_addr, ip->valuestring, sizeof(new_slot_owners[n_slot].addr.ip_addr));
				new_slot_owners[n_slot].addr.p2p_port=(unsigned int)p2p_port->valueint;
				uuid_parse(node_uuid->valuestring, new_slot_owners[n_slot].bin_uuid);
				n_slot++;
			}
		}
		cJSON_free(metadata_array);
		cJSON_free(slot_table);
		sdsfree(decode_buffer);
		sdsfree(resp_body_buff);
	}
	assert(n_slot==0||n_slot==SWARMKV_SLOT_NUM);
	
	if(n_slot==SWARMKV_SLOT_NUM)
	{
		struct key_slot *slot=NULL;
		struct swarmkv_node *owner=NULL;
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots update started.");
		for(i=0; i<n_slot; i++)
		{
			slot=ks->slots+i;
			owner=&(slot->owner);
			pthread_mutex_lock(&slot->mutex);
			if(0!=memcmp(owner, new_slot_owners+i, sizeof(struct swarmkv_node)))
			{
				if(slot->state!=STATE_STABLE)
				{
					MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "slot %d is reset to STABLE state, owner %s:%u.",
						slot->slot_id, owner->addr.ip_addr, owner->addr.p2p_port);
					memset(&(slot->peer), 0, sizeof(slot->peer));
				}
				slot->state=STATE_STABLE;
			}
			swarmkv_node_copy(owner, new_slot_owners+i);
			if(0==swarmkv_node_compare(owner, &(ks->self)))
			{
				slot->I_am_owner=1;
			}
			else
			{
				slot->I_am_owner=0;
				struct key_route_entry *key_entry=NULL, *tmp=NULL;
				size_t key_num=HASH_COUNT(slot->keyroute_table);
				HASH_ITER(hh, slot->keyroute_table, key_entry, tmp)
				{
					HASH_DELETE(hh, slot->keyroute_table, key_entry);
					key_entry_free(key_entry);
				}
				if(key_num>0)
				{
					MESA_handle_runtime_log(ks->logger, RLOG_LV_DEBUG, MODULE_SWARMKV_KEYSPACE, "key slots %d released %zu keys.",
						slot->slot_id, key_num);
				}
			}
			pthread_mutex_unlock(&slot->mutex);
		}
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots update finished.");
	}
	consul_watch_slots_changes_async(ks);
}
static sds keyslots_print_json(struct key_slot slots[], size_t n_slot)
{
	cJSON *slot_array=cJSON_CreateArray();
	sds printed=NULL;
	cJSON *slot=NULL, *node=NULL;
//	cJSON *replica_array=NULL;
	char range[256]="", uuid_str[38]="";
	size_t i=0;
	int start=0, end=0;
	start=end=slots[0].slot_id;
	for(i=0; i<n_slot+1; i++)
	{
		if(i<n_slot
			&& 0==swarmkv_node_compare(&(slots[i].owner), &(slots[end].owner)))
		{
			end=i;
			continue;
		}	
		slot=cJSON_CreateObject();
		if(start==end)
		{
			snprintf(range, sizeof(range), "%d", start);
		}
		else
		{
			snprintf(range, sizeof(range), "%d-%d", start, end);
		}
		cJSON_AddStringToObject(slot, "slot", range);		
		node=cJSON_CreateObject();
		cJSON_AddStringToObject(node, "ip", slots[end].owner.addr.ip_addr);
		cJSON_AddNumberToObject(node, "p2p_port", slots[end].owner.addr.p2p_port);
		uuid_unparse_lower(slots[end].owner.bin_uuid, uuid_str);
		cJSON_AddStringToObject(node, "node_uuid", uuid_str);
		cJSON_AddItemToObject(slot, "owner", node);
		cJSON_AddItemToArray(slot_array, slot);
		start=end=i;
	}
	char* payload=NULL;
	payload=cJSON_Print(slot_array);
//	printf("%s\n", payload);
	printed=sdsnew(payload);
	free(payload);
	cJSON_Delete(slot_array);
	return printed;
}
/*
void keyslots_print_reply(struct key_slot slots[], size_t n_slot, struct swarmkv_reply **pp_reply)
{
	size_t i=0, n_reply=0;
	int start=0, end=0;
	char range[256]="", uuid_str[38]="";
	struct swarmkv_reply **reply_array[SWARMKV_SLOT_NUM];
	const char *slot_state[3]={"STABLE", "MIGRATING", "IMPORTING"};
	start=end=slots[0].slot_id;
	for(i=0; i<n_slot+1; i++)
	{
		if(i<n_slot
			&& 0==swarmkv_node_compare(&(slots[i].owner), &(slots[end].owner))
			&& slots[i].owner==slots[end].state)
		{
			end=i;
			continue;
		}	
		if(start==end)
		{
			snprintf(range, sizeof(range), "%d", start);
		}
		else
		{
			snprintf(range, sizeof(range), "%d-%d", start, end);
		}
		uuid_unparse_lower(slots[end].owner.bin_uuid, uuid_str);
		reply_array[n_reply]=swarmkv_reply_new_string_fmt("%s %s %s:%d [%s]", range, 
																			uuid_str,
																			slots[end].owner.addr.ip_addr,
																			slots[end].owner.addr.p2p_port,
																			slot_state[slots[end].state]
																			);
		start=end=i;
	}

}
*/
void consul_watch_slots_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	struct evhttp_request* request=evhttp_request_new(__watch_slots_changes_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	evhttp_connection_set_timeout(conn, 1800);//set to 30min for blocking query.

	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?index=%d", ks->db_name, ks->consul_slots_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	return;
}
void __propagate_slot_table_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;	
	int resp_code=0;
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed: socket error %s",
			evutil_socket_error_to_string(errcode));	
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
		
	}
	if(resp_code==200)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots table propagate success.");
	}
	else
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed.");
	}
	return;
}

void handle_failed_nodes(struct swarmkv_keyspace* ks, struct swarmkv_node* health_nodes, size_t n_node)
{
	size_t i=0, j=0, k=0;
	size_t slots_number_effect_by_failed_node=0;
	struct key_slot slots_copy[SWARMKV_SLOT_NUM];
	struct swarmkv_node *new_owner=NULL, *old_owner=NULL;
	char uuid_str1[37]="", uuid_str2[37]="";
	memcpy(slots_copy, ks->slots, sizeof(slots_copy));
	if(n_node==0)
	{
		return;
	}
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==swarmkv_node_compare(old_owner, health_nodes+j))
			{
				break;
			}
		}
		//slot owner has left the cluster.
		if(j==n_node)
		{
			slots_number_effect_by_failed_node++;
		}
	}
	int slots_rebalanced_per_node=slots_number_effect_by_failed_node/n_node;
	int health_node_offset=0;
	
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==swarmkv_node_compare(old_owner, health_nodes+j))
			{
				break;
			}
		}
		//slot owner has left the cluster.
		if(j==n_node)
		{
			if(k==slots_rebalanced_per_node && health_node_offset<n_node)
			{
				k=0;
				health_node_offset++;
			}			
			new_owner=health_nodes+health_node_offset;
			uuid_unparse_lower(old_owner->bin_uuid, uuid_str1);
			uuid_unparse_lower(new_owner->bin_uuid, uuid_str2);
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots %d owner is changed from %s %s:%u to %s %s:%u.",
									i, 
									uuid_str1, old_owner->addr.ip_addr, old_owner->addr.p2p_port, 
									uuid_str2, new_owner->addr.ip_addr, new_owner->addr.p2p_port);
			memcpy(old_owner, new_owner, sizeof(struct swarmkv_node));
			k++;
		}
	}
	if(slots_number_effect_by_failed_node)
	{		
		sds new_slots_json=NULL;
		new_slots_json=keyslots_print_json(slots_copy, SWARMKV_SLOT_NUM);
		char url[SWARMKV_URL_MAX]="";
		snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->db_name);
		//Request body(1155868 bytes) too large, max size: 524288 bytes. 
		//See https://www.consul.io/docs/agent/options.html#kv_max_value_size.
		struct evhttp_request* request=evhttp_request_new(__propagate_slot_table_cb, ks);
		struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
		evhttp_connection_set_timeout(conn, 20);
		
		struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
		
		evhttp_add_header(output_headers, "Host", CONSUL_HOST);
		evhttp_add_header(output_headers, "Connection", "close");
		
		struct evbuffer* output_buffer = evhttp_request_get_output_buffer(request);
		evbuffer_add(output_buffer, new_slots_json, sdslen(new_slots_json));
		char number[64];
		snprintf(number, sizeof(number), "%zu", sdslen(new_slots_json));
		evhttp_add_header(output_headers, "Content-Type", "application/json");
		evhttp_add_header(output_headers, "Content-Length", number);
		
		evhttp_make_request(conn, request, EVHTTP_REQ_PUT, url);

		sdsfree(new_slots_json);
		new_slots_json=NULL;
	}
	return;
}
void json2nodes(sds resp_buff, struct swarmkv_node *nodes, size_t *n_node)
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
	cJSON_free(health_reponse);
	*n_node=i;
}
void __watch_nodes_changes_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	int resp_code=0;
	const char* tmp=NULL;
	struct swarmkv_node health_nodes[SWARMKV_NODE_MAX];
	memset(health_nodes, 0, sizeof(health_nodes));
	if(!req || !evhttp_request_get_response_code(req))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "watch node changes failed: %s",
			evutil_socket_error_to_string(errcode));		
	}
	else
	{
		resp_code=evhttp_request_get_response_code(req);
	}
	if(resp_code==200)
	{
		struct evbuffer *buf=NULL;
		buf=evhttp_request_get_input_buffer(req);
		size_t len = evbuffer_get_length(buf);
		size_t n_node=SWARMKV_NODE_MAX;
		sds resp_body_buff=sdsnewlen(NULL, len);
		evbuffer_copyout(buf, resp_body_buff, len);
		json2nodes(resp_body_buff, health_nodes, &n_node);
	
		sdsfree(resp_body_buff);
		struct evkeyvalq *input_headers=evhttp_request_get_input_headers(req);	
		tmp=evhttp_find_header(input_headers, "X-Consul-Index");
		sscanf(tmp, "%d", &ks->consul_nodes_modify_idx);
		if(ks->is_leader)
		{
			handle_failed_nodes(ks, health_nodes, n_node);
			consul_watch_nodes_changes_async(ks);
		}
	}
}
void consul_watch_nodes_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	char tmp[64];

	struct evhttp_request* request=evhttp_request_new(__watch_nodes_changes_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	evhttp_connection_set_timeout(conn, 1800);
	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	snprintf(tmp, sizeof(tmp), "%u", ks->consul_nodes_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1&index=%d", ks->db_name, ks->consul_nodes_modify_idx);
	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	
}

void keyslots_init(struct swarmkv_keyspace* ks)
{
	int i=0;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		ks->slots[i].slot_id=i;
		swarmkv_node_copy(&(ks->slots[i].owner), &(ks->self));
	}
	return;
}
static void consul_kv_init_if_nonexist(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	int resp_code_get=0, resp_code_put=0;
	sds slot_table=NULL, self_info=NULL;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->db_name);
	resp_code_get=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, url, NULL, NULL);
	if(resp_code_get==404)
	{
		slot_table=keyslots_print_json(ks->slots, SWARMKV_SLOT_NUM);
		resp_code_put=http_blocking_request(EVHTTP_REQ_PUT, CONSUL_HOST, ks->consul_agent_port, url, slot_table, NULL);	
		sdsfree(slot_table);
		if(resp_code_put==200)
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "consul KV init slot table %s.", url); 	
		}
		else
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "consul KV init slot table %s failed.", url);	

		}
	}	
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead", ks->db_name);

	resp_code_get=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, url, NULL, NULL);
	if(resp_code_get==404)
	{
		self_info=node_print_json(&(ks->self));
		resp_code_put=http_blocking_request(EVHTTP_REQ_PUT, CONSUL_HOST, ks->consul_agent_port, url, self_info, NULL);	
		sdsfree(self_info);
		if(resp_code_put==200)
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "consul KV init lead %s.", url); 	
		}
		else
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "consul KV init lead %s failed.", url);	

		}
	}

}
struct swarmkv_keyspace* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, void *logger, char **err)
{
	struct swarmkv_keyspace* ks=ALLOC(struct swarmkv_keyspace, 1);
	strncpy(ks->db_name, db_name, sizeof(ks->db_name));
	strncpy(ks->self.addr.ip_addr, opts->bind_address, sizeof(ks->self.addr.ip_addr));
	ks->self.addr.p2p_port=opts->p2p_port;
	ks->health_check_port=opts->health_check_port;
	ks->consul_agent_port=opts->consul_port;
    ks->evbase = event_base_new();
	ks->logger = logger;
	ks->dryrun= opts->dryrun;
	uuid_copy(ks->self.bin_uuid, opts->bin_uuid);

	int ret=-1;
	keyslots_init(ks);
	consul_kv_init_if_nonexist(ks);	
	pthread_create(&(ks->thr), NULL, swarmkv_keyspace_thread, (void *) ks);
	consul_watch_slots_changes_async(ks);
	if(ks->dryrun)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "work on dry run mode.");
		return ks;
	}
	ks->http_server=http_server_new(ks->evbase, ks->health_check_port, ks->logger);
	if(!ks->http_server)
	{
		asprintf(err, "health check HTTP server start failed.");
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "health check HTTP server start failed.");
		goto error_out;
	}

	ret=consul_service_register(ks);
	if(ret<0)
	{	
		asprintf(err, "consul service register failed.");
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "consul service register failed.");
		goto error_out;
	}
	consul_watch_leadership_changes_async(ks);
	
	return ks;

error_out:
	free(ks);
	return NULL;
}

void swarmkv_keyspace_free(struct swarmkv_keyspace* ks)
{
	event_base_loopexit(ks->evbase, NULL);
	while(!event_base_got_exit(ks->evbase))
	{
		usleep(100);
	}
	if(ks->http_server)
	{
		evhttp_free(ks->http_server);
		ks->http_server=NULL;
	}
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	int i=0;
	struct key_slot *slot=NULL;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		slot=ks->slots+i;
		HASH_ITER(hh, slot->keyroute_table, key_entry, tmp)
		{
			HASH_DELETE(hh, slot->keyroute_table, key_entry);
			key_entry_free(key_entry);
		}		
	}
	event_base_free(ks->evbase);
	free(ks);
	return;
}

int swarmkv_keyspace_get_key_owner(struct swarmkv_keyspace* ks, sds key, struct swarmkv_node_addr *owner)
{
	int self_is_keyowner=0;
	int slot_id=get_slot_id(key, sdslen(key));
	struct swarmkv_node keyowner;
	pthread_rwlock_rdlock(&ks->rwlock);
	swarmkv_node_copy(&keyowner, &(ks->slots[slot_id].owner));
	pthread_rwlock_unlock(&ks->rwlock);
	if(0==swarmkv_node_addr_compare(&keyowner.addr, &(ks->self.addr)))
	{
		self_is_keyowner=1;
	}
	memcpy(owner, &(keyowner.addr), sizeof(struct swarmkv_node_addr));
	return self_is_keyowner;
}


int swarmkv_keyspace_is_myself(struct swarmkv_keyspace* ks, const struct swarmkv_node_addr *node)
{
	if(0==memcmp(node, &(ks->self.addr), sizeof(ks->self.addr)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
static void get_slot_keys(struct key_slot *slot, const struct swarmkv_cmd *cmd, struct swarmkv_reply **out)
{
	size_t n_keys_in_slot=0, i=0;
	struct swarmkv_reply *r=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	assert(cmd->op==SWARMKV_OP_KEYSPACE_GETKEYSINSLOT);
	n_keys_in_slot=HASH_COUNT(slot->keyroute_table);
	r=swarmkv_reply_new_array(n_keys_in_slot*3);				
	HASH_ITER(hh, slot->keyroute_table, key_entry, tmp)
	{
		r->elements[i]=swarmkv_reply_new_string(key_entry->key, sdslen(key_entry->key));
		node2reply(&(key_entry->owner), r->elements+1);
		i+=3;
	}
	*out=r;
	return;
}
static void set_slot(struct key_slot *slot, const struct swarmkv_cmd *cmd, struct swarmkv_reply **out)
{
	assert(cmd->op==SWARMKV_OP_KEYSPACE_SETSLOT);
	struct swarmkv_node_addr node;
	//KEYSPACE SETSLOT slot_id IMPORTING host port|MIGRATING host port|NODE host port|STABLE
	*out=NULL;
	if(cmd->n_args==4)
	{
		sds2node_addr(cmd->args+2, &node);
	}
	if(strcmp(cmd->args[1], "IMPORTING"))
	{
		slot->state=STATE_IMPORTING;
		memcpy(&(slot->peer), &node, sizeof(node));
	}
	else if(strcmp(cmd->args[1], "MIGRATING"))
	{
		slot->state=STATE_MIGRATING;
		memcpy(&(slot->peer), &node, sizeof(node));
	}
	else if(strcmp(cmd->args[1], "NODE"))
	{
		slot->state=STATE_STABLE;
		memcpy(&(slot->owner), &node, sizeof(node));
	}
	else if(strcmp(cmd->args[1], "STABLE"))
	{
		slot->state=STATE_STABLE;
	}
	else
	{
		assert(0);
		*out=swarmkv_reply_new_error("invalid argument");
	}
	if(!*out) *out=swarmkv_reply_new_status("OK");
	return;
}
static int consul_get_cluster_leader_block(const char *db_name, unsigned int consul_agent_port, struct swarmkv_node *leader)
{
	cJSON *leader_json=NULL, *item=NULL;
	sds resp_body=NULL;
	int resp_code=0;
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url),"/v1/kv/swarmkv/%s/lead?raw=1", db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	leader_json=cJSON_Parse(resp_body);
	item=cJSON_GetObjectItem(leader_json, "ip");
	strncpy(leader->addr.ip_addr, item->valuestring, sizeof(leader->addr.ip_addr));
	item=cJSON_GetObjectItem(leader_json, "p2p_port");
	leader->addr.p2p_port=(unsigned int)item->valueint;
	item=cJSON_GetObjectItem(leader_json, "node_uuid");
	uuid_parse(item->valuestring, leader->bin_uuid);
	sdsfree(resp_body);
	return 0;
}
static int consul_list_health_nodes_block(const char *db_name, unsigned int consul_agent_port, struct swarmkv_node *nodes, size_t *n_nodes)
{
	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1", db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	json2nodes(resp_body, nodes, n_nodes);
	sdsfree(resp_body);
	return 0;
}
static void get_cluster_nodes(struct swarmkv_keyspace* ks, struct swarmkv_reply **pp_reply)
{
	int ret=0;
	struct swarmkv_node leader; 	
	struct swarmkv_node nodes[SWARMKV_NODE_MAX];
	size_t n_node=SWARMKV_NODE_MAX;
	ret=consul_get_cluster_leader_block(ks->db_name, ks->consul_agent_port, &leader);
	if(ret<0)
	{
		*pp_reply=swarmkv_reply_new_error(error_cluster_leader_not_found);
		return;
	}
	ret=consul_list_health_nodes_block(ks->db_name, ks->consul_agent_port, nodes, &n_node);
	if(ret<0)
	{
		*pp_reply=swarmkv_reply_new_error(error_cluster_leader_not_found);
		return;
	}
	size_t i=0;
	char uuid_str[37];
	int is_leader;
	struct swarmkv_reply *r=swarmkv_reply_new_array(n_node);
	for(i=0; i<n_node; i++)
	{
		uuid_unparse_lower(nodes[i].bin_uuid, uuid_str);
		if(0==swarmkv_node_compare(&leader, nodes+i))
		{
			is_leader=1;
		}
		else
		{
			is_leader=0;
		}
		r->elements[i]=swarmkv_reply_new_string_fmt("%s %s:%u %s", uuid_str,
																nodes[i].addr.ip_addr,
																nodes[i].addr.p2p_port,
																is_leader?"leader":"follower");
	}
	*pp_reply=r;
	return;
}
void swarmkv_keyspace_exec(struct swarmkv_keyspace* ks, const struct swarmkv_cmd *cmd, struct swarmkv_reply **pp_reply)
{
	struct swarmkv_reply *r=NULL;
	struct key_route_entry *key_entry=NULL;
	int slot_id=-1;
	struct swarmkv_node_addr obj_owner;
	struct key_slot* slot=NULL;

	//key slot operations
	if(cmd->op==SWARMKV_OP_KEYSPACE_GETKEYSINSLOT)
	{
		slot_id=(int)atoll(cmd->args[0]);
		slot=&(ks->slots[slot_id]);
		if(!swarmkv_keyspace_is_myself(ks, &(slot->owner.addr)))
		{
			*pp_reply=swarmkv_reply_new_error("node is not slot owner");
			return;
		}
		pthread_mutex_lock(&slot->mutex);
		get_slot_keys(slot, cmd, pp_reply);
		pthread_mutex_unlock(&slot->mutex);
		return;
	}
	if(cmd->op==SWARMKV_OP_KEYSPACE_SETSLOT)
	{
		slot_id=(int)atoll(cmd->args[0]);
		slot=&(ks->slots[slot_id]);
		
		pthread_mutex_lock(&slot->mutex);
		set_slot(slot, cmd, pp_reply);	
		pthread_mutex_unlock(&slot->mutex);
		return;
	}
	//key owner operations
	slot_id=get_slot_id(cmd->args[0], sdslen(cmd->args[0]));
	slot=&(ks->slots[slot_id]);
	
	if( 0==swarmkv_node_compare(&(slot->owner), &(ks->self))&& slot->state==STATE_STABLE)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, 
			"weird behavior: set an object owner (key=%.5s) of slot %d, which key owner should be %s:%u",
			cmd->args[0], slot_id, slot->owner.addr.ip_addr, slot->owner.addr.p2p_port);	
	}
	
	HASH_FIND(hh, slot->keyroute_table, cmd->args[0], sdslen(cmd->args[0]), key_entry);
	
	if(!key_entry && slot->state==STATE_MIGRATING)
	{
		r=swarmkv_reply_new_array(3);
		r->elements[0]=swarmkv_reply_new_string("ASK", 4);
		r->elements[1]=swarmkv_reply_new_string(slot->peer.addr.ip_addr, strlen(slot->peer.addr.ip_addr)+1);
		r->elements[2]=swarmkv_reply_new_integer(slot->peer.addr.p2p_port);
		*pp_reply=r;
		pthread_mutex_unlock(&slot->mutex);
		return;
	}

	switch(cmd->op)
	{
		case SWARMKV_OP_KEYSPACE_SET://KEYSPACE SET key obj_owner_ip obj_owner_port
			strncpy(obj_owner.ip_addr, cmd->args[1], sizeof(obj_owner.ip_addr));
			obj_owner.p2p_port=(unsigned int)atoll(cmd->args[2]);
			if(key_entry)
			{
				memcpy(&(key_entry->owner), &obj_owner, sizeof(obj_owner));
			}
			else
			{
				key_entry=key_entry_new(cmd->args[0], &obj_owner);
				HASH_ADD_KEYPTR(hh, slot->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			}
			r=swarmkv_reply_new_status("OK");
			break;
		case SWARMKV_OP_KEYSPACE_SETNXGET: //KEYSPACE SETNXGET key obj_owner_ip obj_owner_port
			strncpy(obj_owner.ip_addr, cmd->args[1], sizeof(obj_owner.ip_addr));
			obj_owner.p2p_port=(unsigned int)atoll(cmd->args[2]);
			if(!key_entry)
			{
				key_entry=key_entry_new(cmd->args[0], &obj_owner);
				HASH_ADD_KEYPTR(hh, slot->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			}
			r=swarmkv_reply_new_array(2);
			node2reply(&(key_entry->owner), r->elements);
			break;
		case SWARMKV_OP_KEYSPACE_GET:
			if(key_entry)
			{
				r=swarmkv_reply_new_array(2);
				node2reply(&(key_entry->owner), r->elements);
			}
			else
			{
				r=swarmkv_reply_new_nil();
			}
			break;
		case SWARMKV_OP_KEYSPACE_DEL:
			if(key_entry)
			{
				HASH_DELETE(hh, slot->keyroute_table, key_entry);
				r=swarmkv_reply_new_array(2);
				node2reply(&(key_entry->owner), r->elements);
				key_entry_free(key_entry);
			}
			else
			{
				r=swarmkv_reply_new_nil();
			}
			break;
		default:
			assert(0);
			break;
	}
	*pp_reply=r;
	
	pthread_mutex_unlock(&slot->mutex);
	return;
}
 struct swarmkv_cmd *swarmkv_keyspace_cmd_new(enum swarmkv_operator store_op, const sds key, const struct swarmkv_node_addr *in)
 {
	struct swarmkv_cmd *cmd=NULL;
	if(is_store_read_operator(store_op))
	{
		cmd=swarmkv_cmd_new(SWARMKV_OP_KEYSPACE_GET, 1);
		cmd->args[0]=sdsdup(key);
	}
	else if(store_op==SWARMKV_OP_DEL)
	{
		cmd=swarmkv_cmd_new(SWARMKV_OP_KEYSPACE_DEL, 1);
		cmd->args[0]=sdsdup(key);		
	}
	else
	{
		cmd=swarmkv_cmd_new(SWARMKV_OP_KEYSPACE_SETNXGET, 3);
		cmd->args[0]=sdsdup(key);
		cmd->args[1]=sdsnew(in->ip_addr);
		cmd->args[2]=sdsfromlonglong(in->p2p_port);
	}
	return cmd;
 }
enum key_route_result swarmkv_keyspace_reply2node(enum swarmkv_operator keyspace_op, const struct swarmkv_reply *reply, struct swarmkv_node_addr *out)
{
	int offset_node_ip=0;
	enum key_route_result ret=__INVALID;
	if(keyspace_op==SWARMKV_OP_KEYSPACE_SETNXGET ||
		keyspace_op==SWARMKV_OP_KEYSPACE_GET ||
		keyspace_op==SWARMKV_OP_KEYSPACE_DEL)
	{
		if(reply->type==SWARMKV_REPLY_ARRAY)
		{
			if(0==strncasecmp(reply->elements[0]->str, "ASK", reply->elements[0]->len))
			{
				offset_node_ip++;
				ret=ASK;
			}
			else
			{
				ret=FOUND;
			}
			strncpy(out->ip_addr, reply->elements[offset_node_ip]->str, sizeof(out->ip_addr));
			out->p2p_port=(unsigned int)(reply->elements[offset_node_ip+1]->integer);
		}
		else
		{
			ret=NOT_FOUND;
		}
	}
	return ret;
}
void swarmkv_keyspace_get_cluster_nodes(struct swarmkv_keyspace *ks, struct swarmkv_reply **pp_reply)
{
	get_cluster_nodes(ks, pp_reply);
	return;
}
static int consul_get_slots_block(const char *db_name, unsigned int consul_agent_port,  struct swarmkv_reply **pp_reply)
{
	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0, n_record=0, i=0;
	struct swarmkv_reply *r=NULL;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?raw=1", db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	cJSON *slot_table=NULL, *range=NULL, *slot=NULL, *owner=NULL, *ip=NULL, *p2p_port=NULL, *node_uuid=NULL;
	slot_table=cJSON_Parse(resp_body);
	n_record=cJSON_GetArraySize(slot_table);
	r=swarmkv_reply_new_array((size_t)n_record);
	for(i=0; i<n_record; i++)
	{
		slot=cJSON_GetArrayItem(slot_table, i);
		range=cJSON_GetObjectItem(slot, "slot");
		owner=cJSON_GetObjectItem(slot, "owner");	
		ip=cJSON_GetObjectItem(owner, "ip");	
		p2p_port=cJSON_GetObjectItem(owner, "p2p_port");
		node_uuid=cJSON_GetObjectItem(owner, "node_uuid");
		r->elements[i]=swarmkv_reply_new_string_fmt("%s %s %s:%d", range->valuestring,
																		node_uuid->valuestring,
																		ip->valuestring,
																		p2p_port->valueint);
		
	}
	cJSON_free(slot_table);
	sdsfree(resp_body);
	*pp_reply=r;
	return 0;
}

void swarmkv_keyspace_get_cluster_slots(struct swarmkv_keyspace *ks, struct swarmkv_reply **pp_reply)
{
	int ret=0;
	ret=consul_get_slots_block(ks->db_name, ks->consul_agent_port, pp_reply);
	if(ret<0)
	{
		*pp_reply=swarmkv_reply_new_error(error_read_slot_table_failed);
	}
	return;
}

