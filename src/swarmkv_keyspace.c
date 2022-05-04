#include "swarmkv_keyspace.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include "swarmkv_net.h"
#include "swarmkv_error.h"

#include "cJSON.h"
#include "sds.h"
#include "mpack.h"
#include "base64.h"
#include "utarray.h"
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

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>


#define	CONSUL_HOST	"127.0.0.1"
#define	CONSUL_DEFAULT_PORT	8500

#define	LOCAL_HOST	"127.0.0.1"


#define MODULE_SWARMKV_KEYSPACE		module_name_str("swarmkv.keyspace")



struct key_route_entry
{
	sds key;	
	uint64_t abs_timeout;
	struct swarmkv_node_addr owner;
	UT_hash_handle hh;
};
struct key_route_entry *key_entry_new_len(const char* key, size_t key_len, const struct swarmkv_node_addr* owner)
{
	struct key_route_entry *entry=NULL;
	entry=ALLOC(struct key_route_entry, 1);
	entry->key=sdsnewlen(key, key_len);
	swarmkv_node_addr_copy(&(entry->owner), owner);
	return entry;

}
struct key_route_entry *key_entry_new(const sds key, const struct swarmkv_node_addr* owner)
{
	struct key_route_entry *entry=NULL;
	entry=key_entry_new_len(key, sdslen(key), owner);
	return entry;
}

void key_entry_free(struct key_route_entry *entry)
{
	sdsfree(entry->key);
	entry->key=NULL;
	free(entry);
}
struct slot_runtime
{
	struct key_slot slot;
	enum slot_state state;
	struct swarmkv_node peer;//peer is used for state MIGRATING and IMPORTING

	struct key_route_entry* keyroute_table;
	struct timeouts *expires;

	int I_am_owner;
	pthread_mutex_t mutex;
};
struct consul_task
{
	char task_name[SWARMKV_SYMBOL_MAX];
	struct evhttp_connection* evhttpconn; //tcp connection for the request
	UT_hash_handle hh;
};
struct consul_task *consul_task_new(const char* task_name, struct event_base *evbase, const char* host_name, unsigned short port)
{	
	struct consul_task *task=ALLOC(struct consul_task, 1);
	strncpy(task->task_name, task_name, sizeof(task->task_name));
	task->evhttpconn=evhttp_connection_base_new(evbase, NULL, host_name, port);
	return task;
}
void consul_task_free(struct consul_task *task)
{
	evhttp_connection_free(task->evhttpconn);
	task->evhttpconn=NULL;
	free(task);
}
struct swarmkv_keyspace
{
	struct swarmkv_module module;
	char db_name[SWARMKV_SYMBOL_MAX];
	pthread_t thr;
	int readable_tid;

	struct swarmkv_node self;
	pthread_rwlock_t rwlock;	
//	struct key_slot slots[SWARMKV_SLOT_NUM];
	struct slot_runtime slot_rts[SWARMKV_SLOT_NUM];

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
	struct consul_task *consul_task_hash;
	//reference, no need free
	void *logger;
};
struct consul_task *consul_task_getnxset(struct swarmkv_keyspace* ks, const char* task_name)
{
	struct consul_task *task=NULL;
	HASH_FIND(hh, ks->consul_task_hash, task_name, strlen(task_name), task);
	if(!task)
	{
		task=consul_task_new(task_name, ks->evbase, CONSUL_HOST, (unsigned short)ks->consul_agent_port);
		HASH_ADD(hh, ks->consul_task_hash, task_name, strlen(task_name), task);
	}
	return task;
}
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
	evbuffer_free(evbuf_reply);
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
	resp_code=http_blocking_request(EVHTTP_REQ_PUT, CONSUL_HOST, ks->consul_agent_port, "/v1/agent/service/register?replace-existing-checks=true",
		req_body,  &resp_body);
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
	event_del(ev);
	event_free(ev);

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
	cJSON_Delete(session);
//	printf("%s\n", req_body);

	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/create");

	struct consul_task *task=consul_task_getnxset(ks, __func__);

	struct evhttp_request* request=evhttp_request_new(__consul_session_create_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;	
	evhttp_connection_set_timeout(conn, 2);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");
	
	struct evbuffer* output_buffer = evhttp_request_get_output_buffer(request);
	evbuffer_add(output_buffer, payload, strlen(payload));
	char number[64];
	snprintf(number, sizeof(number), "%zu", strlen(payload));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	evhttp_make_request(conn, request, EVHTTP_REQ_PUT, url);
	free(payload);
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
	struct consul_task *task=consul_task_getnxset(ks, __func__);

	struct evhttp_request* request=evhttp_request_new(__consul_session_check_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;
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
	cJSON_Delete(node_info);
	node_info=NULL;
	
	sds self_json=NULL;
	self_json=sdsnew(payload);
	free(payload);
	return self_json;
}
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks)
{
	sds req_body=node_print_json(&(ks->self));

	char url[SWARMKV_URL_MAX];
	struct consul_task *task=consul_task_getnxset(ks, __func__);
	
	//run for leader
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?acquire=%s", ks->db_name, ks->consul_session_id);

	struct evhttp_request* request=evhttp_request_new(__acquire_session_lock_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;
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
			cJSON_Delete(metadata_array);
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
	struct consul_task *task=consul_task_getnxset(ks, __func__);
	
	struct evhttp_request* request=evhttp_request_new(__watch_leadership_changes_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;
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
	int resp_code=0;
	int	i=0;

	struct key_slot *new_slots=NULL;

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
	if(resp_code!=200)
	{		
		consul_watch_slots_changes_async(ks);
		return;
	}		

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

	new_slots=ALLOC(struct key_slot, SWARMKV_SLOT_NUM);
	json2keyslots(decode_buffer, new_slots, sizeof(struct key_slot), 0, SWARMKV_SLOT_NUM);

	sdsfree(decode_buffer);
	sdsfree(resp_body_buff);

	struct key_slot *slot=NULL;
	struct slot_runtime *slot_rt=NULL;
	struct swarmkv_node *owner=NULL;
	MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots update started.");
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;	
		slot=&(slot_rt->slot);
		owner=&(slot->owner);
		pthread_mutex_lock(&slot_rt->mutex);
		if(0!=swarmkv_node_compare(owner, &(new_slots[i].owner)))
		{
			if(slot_rt->state!=STATE_STABLE)
			{
				MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "slot %d is reset to STABLE state, owner %s:%u.",
					slot->slot_id, owner->addr.ip_addr, owner->addr.p2p_port);
				memset(&(slot_rt->peer), 0, sizeof(slot_rt->peer));
			}
			slot_rt->state=STATE_STABLE;
		}
		swarmkv_node_copy(owner, &(new_slots[i].owner));
		if(0==swarmkv_node_addr_compare(&owner->addr, &(ks->self.addr)))
		{
			slot_rt->I_am_owner=1;
		}
		pthread_mutex_unlock(&slot_rt->mutex);
	}
	MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots update finished.");
	cJSON_Delete(metadata_array);
	consul_watch_slots_changes_async(ks);
	free(new_slots);
}

/*
void keyslots_print_reply(struct slot_runtime slots[], size_t n_slot, struct swarmkv_reply **pp_reply)
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
	struct consul_task *task=consul_task_getnxset(ks, __func__);
	
	struct evhttp_request* request=evhttp_request_new(__watch_slots_changes_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;	
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
void propagate_slot_table_async(struct swarmkv_keyspace *ks, struct key_slot new_slot[], size_t slot_num)
{
	struct consul_task *task=consul_task_getnxset(ks, __func__);
	
	sds new_slots_json=NULL;
	new_slots_json=keyslots2json(new_slot, sizeof(struct key_slot), 0, SWARMKV_SLOT_NUM);
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->db_name);
	struct evhttp_request* request=evhttp_request_new(__propagate_slot_table_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;	
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
void handle_failed_nodes(struct swarmkv_keyspace* ks, struct swarmkv_node* health_nodes, size_t n_node)
{
	size_t i=0, j=0, k=0;
	size_t slots_number_effect_by_failed_node=0;
	struct key_slot *slots_copy=NULL;
	struct swarmkv_node *new_owner=NULL, *old_owner=NULL;
//	char uuid_str1[37]="", uuid_str2[37]="";
	if(n_node==0)
	{
		return;
	}
	slots_copy=ALLOC(struct key_slot, SWARMKV_SLOT_NUM);
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		memcpy(slots_copy+i, &(ks->slot_rts[i].slot), sizeof(struct key_slot));
	}
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==swarmkv_node_addr_compare(&(old_owner->addr), &(health_nodes[j].addr)))
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
	//For generating compact slot assignment
	//Request body(1155868 bytes) too large, max size: 524288 bytes. 
	//See https://www.consul.io/docs/agent/options.html#kv_max_value_size.
	int slots_rebalanced_per_node=slots_number_effect_by_failed_node/n_node;
	int health_node_offset=0;
	
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==swarmkv_node_addr_compare(&(old_owner->addr), &(health_nodes[j].addr)))
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
			/*
			uuid_unparse_lower(old_owner->bin_uuid, uuid_str1);
			uuid_unparse_lower(new_owner->bin_uuid, uuid_str2);
			MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots %d owner is changed from %s %s:%u to %s %s:%u.",
									i, 
									uuid_str1, old_owner->addr.ip_addr, old_owner->addr.p2p_port, 
									uuid_str2, new_owner->addr.ip_addr, new_owner->addr.p2p_port);
			*/
			memcpy(old_owner, new_owner, sizeof(struct swarmkv_node));
			k++;
		}
	}
	if(slots_number_effect_by_failed_node)
	{
		propagate_slot_table_async(ks, slots_copy, SWARMKV_SLOT_NUM);
	}
	free(slots_copy);
	slots_copy=NULL;
	return;
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
	struct consul_task *task=consul_task_getnxset(ks, __func__);
	
	struct evhttp_request* request=evhttp_request_new(__watch_nodes_changes_cb, ks);
	struct evhttp_connection* conn=task->evhttpconn;	
	evhttp_connection_set_timeout(conn, 1800);
	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	snprintf(tmp, sizeof(tmp), "%u", ks->consul_nodes_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1&index=%d", ks->db_name, ks->consul_nodes_modify_idx);
	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	
}

int keyslots_init(struct swarmkv_keyspace* ks)
{
	int i=0;
	struct key_slot *slot=NULL;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		slot=&(ks->slot_rts[i].slot);
		slot->slot_id=i;
		swarmkv_node_copy(&(slot->owner), &(ks->self));
	}

	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?raw=1", ks->db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	json2keyslots(resp_body, ks->slot_rts, sizeof(struct slot_runtime), offsetof(struct slot_runtime, slot), SWARMKV_SLOT_NUM);
	sdsfree(resp_body);
	return 0;
}
/*
static void consul_kv_init_if_nonexist(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	int resp_code_get=0, resp_code_put=0;
	sds self_info=NULL;
	char *slot_table=NULL;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->db_name);
	resp_code_get=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, url, NULL, NULL);
	if(resp_code_get==404)
	{
		slot_table=keyslots2json(ks->slots, SWARMKV_SLOT_NUM);
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
*/
static struct swarmkv_keyspace *module2keyspace(struct swarmkv_module *module)
{
	//type safety check
	assert(0==strcmp(module->name, "keyspace"));
	struct swarmkv_keyspace *ks=container_of(module, struct swarmkv_keyspace, module);
	assert(ks==module->mod_ctx);
	return ks;
}

struct swarmkv_module* swarmkv_keyspace_new(const struct swarmkv_options *opts, const char* db_name, void *logger, char **err)
{
	struct swarmkv_keyspace* ks=ALLOC(struct swarmkv_keyspace, 1);
	strncpy(ks->module.name, "keyspace", sizeof(ks->module.name));
	ks->module.mod_ctx=ks;
	pthread_rwlock_init(&(ks->module.rwlock), NULL);
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
	ret=keyslots_init(ks);
	if(ret<0)
	{
		asprintf(err, "key slots init failed.");		
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "key slots init failed.");
		goto error_out;
	}
//	consul_kv_init_if_nonexist(ks);	
	pthread_create(&(ks->thr), NULL, swarmkv_keyspace_thread, (void *) ks);
	consul_watch_slots_changes_async(ks);
	if(ks->dryrun)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "work on dry run mode.");
		return &(ks->module);
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
	
	return &(ks->module);

error_out:
	free(ks);
	return NULL;
}

void swarmkv_keyspace_free(struct swarmkv_module* module)
{
	struct swarmkv_keyspace *ks = module2keyspace(module);

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
	struct key_route_entry *key_entry=NULL, *tmp_key_entry=NULL;
	int i=0;
	struct slot_runtime *slot_rt=NULL;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp_key_entry)
		{
			HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
			key_entry_free(key_entry);
		}		
	}
	struct consul_task *task=NULL, *tmp_task=NULL;
	HASH_ITER(hh, ks->consul_task_hash, task, tmp_task)
	{
		HASH_DELETE(hh, ks->consul_task_hash, task);
		consul_task_free(task);
	}
	event_base_free(ks->evbase);
	pthread_rwlock_destroy(&(ks->module.rwlock));
	free(ks);
	return;
}

static int swarmkv_keyspace_get_key_owner(struct swarmkv_keyspace* ks, const sds key, struct swarmkv_node_addr *owner)
{
	int self_is_keyowner=0;
	int slot_id=get_slot_id(key, sdslen(key));
	struct slot_runtime *slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	swarmkv_node_addr_copy(owner, &(slot_rt->slot.owner.addr));
	pthread_mutex_unlock(&slot_rt->mutex);
	if(0==swarmkv_node_addr_compare(owner, &(ks->self.addr)))
	{
		self_is_keyowner=1;
	}
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




struct swarmkv_reply *make_keyspace_reply(enum key_route_result ret, const struct swarmkv_node_addr *peer)
{
	struct swarmkv_reply *reply=NULL;
	switch(ret)
	{
		case NOT_FOUND:
			reply=swarmkv_reply_new_nil();
			break;
		case ASK:
			reply=swarmkv_reply_new_array(3);
			reply->elements[0]=swarmkv_reply_new_string("ASK", 4);
			reply->elements[1]=swarmkv_reply_new_string(peer->ip_addr, strlen(peer->ip_addr)+1);
			reply->elements[2]=swarmkv_reply_new_integer(peer->p2p_port);
			break;
		case FOUND:
			reply=swarmkv_reply_new_array(2);
			node_addr2reply(peer, reply->elements);
			break;
		default:
			assert(0);
			break;
	}
	return reply;
}

enum key_route_result swarmkv_keyspace_reply2node(const struct swarmkv_reply *reply, struct swarmkv_node_addr *out)
{
	int offset_node_ip=0;
	enum key_route_result ret=__INVALID;

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
		memset(out, 0, sizeof(struct swarmkv_node_addr));
		strncpy(out->ip_addr, reply->elements[offset_node_ip]->str, sizeof(out->ip_addr));
		out->p2p_port=(unsigned int)(reply->elements[offset_node_ip+1]->integer);
	}
	else
	{
		ret=NOT_FOUND;
	}
	
	return ret;
}


void keyspace_setslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	/*KEYSPACE SETSLOT node-ip node-port <slot> IMPORTING ip port | MIGRATING ip port | NODE ip port | STABLE*/

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int slot_id=-1;
	struct swarmkv_node_addr target, peer, *p=NULL;

	if(cmd->argc<6)
	{
		*reply=swarmkv_reply_new_error(error_wrong_number_of_arg);
		return;
	}

	sds2node_addr(cmd->argv+2, &target);
	assert(0==swarmkv_node_addr_compare(&(ks->self.addr), &(target)));
	enum slot_state state;
	sds str_state=cmd->argv[5];
	p=&peer;
	if(0==strcasecmp(str_state, "IMPORTING"))
	{
		state=STATE_IMPORTING;
		sds2node_addr(cmd->argv+6, &peer);
	}
	else if(0==strcasecmp(str_state, "MIGRATING"))
	{
		state=STATE_MIGRATING;
		sds2node_addr(cmd->argv+6, &peer);
	}
	else if(0==strcasecmp(str_state, "NODE"))
	{
		state=STATE_STABLE;
		sds2node_addr(cmd->argv+6, &peer);
	}
	else if(0==strcasecmp(str_state, "STABLE"))
	{
		state=STATE_STABLE;
		p=NULL;
	}
	else
	{
		*reply=swarmkv_reply_new_error("unknwon state %s", str_state);
		return;
	}
	struct slot_runtime *slot_rt=NULL;
	
	slot_id=(int)atoll(cmd->argv[2]);
	slot_rt=&(ks->slot_rts[slot_id]);
	
	pthread_mutex_lock(&slot_rt->mutex);
	slot_rt->state=state;
	if(p)
	{
		swarmkv_node_addr_copy(&(slot_rt->peer.addr), p);
	}
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_status("OK");
	return;
}
enum cmd_fastpath_result keyspace_setslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_fastpath_result ret=0;

	struct swarmkv_node_addr target;
	sds2node_addr(cmd->argv+2, &target);
	
	if(0!=swarmkv_node_addr_compare(&(ks->self.addr), &(target)))
	{
		*reply=swarmkv_reply_new_array(2);
		node_addr2reply(&target, (*reply)->elements);
		ret=REDIRECT;
	}
	else
	{
		keyspace_setslot_command_as_owner(mod_keyspace, cmd, NULL, reply);
		ret=FINISHED;
	}

	return ret;

}
static sds key_slot_serialize(const struct slot_runtime *slot_rt)
{
	sds blob=NULL;
	mpack_writer_t snapshot_mpack;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	size_t key_cnt=0;
	key_cnt=HASH_CNT(hh, slot_rt->keyroute_table);

	mpack_writer_init_growable(&snapshot_mpack, &mpack_buff, &mpack_sz);
	mpack_build_map(&snapshot_mpack);

	mpack_write_cstr(&snapshot_mpack, "slot_id");
	mpack_write_int(&snapshot_mpack, slot_rt->slot.slot_id);

	mpack_write_cstr(&snapshot_mpack, "key_cnt");
	mpack_write_int(&snapshot_mpack, key_cnt);


	mpack_write_cstr(&snapshot_mpack, "key_route_table");	
	mpack_build_array(&snapshot_mpack);
	HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp)
	{
		mpack_write_str(&snapshot_mpack, key_entry->key, sdslen(key_entry->key));
		mpack_write_u64(&snapshot_mpack, key_entry->abs_timeout);
		mpack_write_cstr(&snapshot_mpack, key_entry->owner.ip_addr);
		mpack_write_int(&snapshot_mpack, key_entry->owner.p2p_port);
	}
	mpack_complete_array(&snapshot_mpack);
	mpack_complete_map(&snapshot_mpack);
	mpack_writer_destroy(&snapshot_mpack);
	blob=sdsnewlen(mpack_buff, mpack_sz);
	free(mpack_buff);
	return blob;
}
long long key_slot_merge_blob(struct slot_runtime *slot_rt, const sds blob)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, sdslen(blob));
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t key_item, ip_item, port_item, timeout_item, table;
	int slot_id=mpack_node_int(mpack_node_map_cstr(root, "slot_id"));
	assert(slot_id==slot_rt->slot.slot_id);
	table=mpack_node_map_cstr(root, "key_route_table");

	long long new_added_entry_num=0;
	
	size_t key_cnt=mpack_node_int(mpack_node_map_cstr(root, "key_cnt"));
	size_t entry_num=mpack_node_array_length(table);
	assert(key_cnt*3==entry_num);
	struct key_route_entry *key_entry=NULL;
	struct swarmkv_node_addr obj_owner;
	
	for(size_t i=0; i<entry_num; i+=3)
	{
		key_item=mpack_node_array_at(table, i);
		timeout_item=mpack_node_array_at(table, i+1);
		ip_item=mpack_node_array_at(table, i+2);
		port_item=mpack_node_array_at(table, i+3);
		mpack_node_copy_cstr(ip_item, obj_owner.ip_addr, sizeof(obj_owner.ip_addr));
		obj_owner.p2p_port=mpack_node_int(port_item);
		HASH_FIND(hh, slot_rt->keyroute_table, mpack_node_str(key_item), mpack_node_strlen(key_item), key_entry);
		if(!key_entry)
		{
			key_entry=key_entry_new_len(mpack_node_str(key_item), mpack_node_strlen(key_item), &obj_owner);
			HASH_ADD_KEYPTR(hh, slot_rt->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			new_added_entry_num++;
		}
		else
		{
			swarmkv_node_addr_copy(&key_entry->owner, &obj_owner);
		}
		key_entry->abs_timeout=mpack_node_u64(timeout_item);
	}
	if(mpack_tree_destroy(&tree)!= mpack_ok)
	{
		assert(0);
	}
	return new_added_entry_num;
}

void keyspace_getkeysinslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int slot_id=-1;
	struct slot_runtime* slot_rt=NULL;

	slot_id=(int)atoll(cmd->argv[4]);
	assert(slot_id<SWARMKV_SLOT_NUM);
	slot_rt=ks->slot_rts+slot_id;
	assert(0==swarmkv_node_addr_compare(&(ks->self.addr), &(slot_rt->slot.owner.addr)));
	sds blob=NULL;
	pthread_mutex_lock(&slot_rt->mutex);
	blob=key_slot_serialize(slot_rt);
	pthread_mutex_unlock(&slot_rt->mutex);


	*reply=swarmkv_reply_new_string(blob, sdslen(blob));
	sdsfree(blob);

	return;
}


enum cmd_fastpath_result keyspace_getkeysinslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE GETKEYSINSLOT IP port slot*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_fastpath_result ret=0;
	struct swarmkv_node_addr asking_node;
	
	sds2node_addr(cmd->argv+2, &asking_node);

	if(0!=swarmkv_node_addr_compare(&(ks->self.addr), &asking_node))
	{
		*reply=make_keyspace_reply(ASK, &asking_node);
		ret=REDIRECT;
	}
	else
	{
		keyspace_getkeysinslot_command_as_owner(mod_keyspace, cmd, NULL, reply);
		ret=FINISHED;
	}

	return ret;
}
void keyspace_addkeystoslot_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	/*KEYSPACE ADDKEYSTOSLOT IP port slot blob*/

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int slot_id=-1;
	long long new_added_entry_num=0;
	struct slot_runtime* slot_rt=NULL;
	const sds blob=cmd->argv[5];

	slot_id=(int)atoll(cmd->argv[4]);
	slot_rt=ks->slot_rts+slot_id;

	assert(cmd->argc==6);
	
	pthread_mutex_lock(&slot_rt->mutex);
	new_added_entry_num=key_slot_merge_blob(slot_rt, blob);
	pthread_mutex_unlock(&slot_rt->mutex);
	
	*reply=swarmkv_reply_new_integer(new_added_entry_num);
}
enum cmd_fastpath_result keyspace_addkeystoslot_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE ADDKEYSTOSLOT IP port slot key owner-IP owner-port [key ...]*/

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	struct swarmkv_node_addr asking_node;
	
	sds2node_addr(cmd->argv+2, &asking_node);

	if(0==swarmkv_node_addr_compare(&(ks->self.addr), &asking_node))
	{
		keyspace_addkeystoslot_command_as_owner(mod_keyspace, cmd, NULL, reply);
		return FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &asking_node);
		return REDIRECT;
	}
}
void keyspace_delslotkeys_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE DELSLOTKEYS IP port slot*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int slot_id=-1;
	struct slot_runtime* slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	long long delete_num=0;
	slot_id=(int)atoll(cmd->argv[4]);
	slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp)
	{
		HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
		key_entry_free(key_entry);
		delete_num++;
	}
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_integer(delete_num);
}
enum cmd_fastpath_result keyspace_delslotkeys_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE DELSLOTKEYS IP port slot*/

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	struct swarmkv_node_addr asking_node;
	
	sds2node_addr(cmd->argv+2, &asking_node);

	if(0==swarmkv_node_addr_compare(&(ks->self.addr), &asking_node))
	{
		keyspace_delslotkeys_command_as_owner(mod_keyspace, cmd, NULL, reply);
		return FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &asking_node);
		return REDIRECT;
	}

}
enum KEYSPACE_OP
{
	KEYSPACE_GET,
	KEYSPACE_SET,
	KEYSPACE_SETNXGET,
	KEYSPACE_DEL
};

enum key_route_result keyspace_generic(struct swarmkv_keyspace *ks, enum KEYSPACE_OP op, const sds key, const struct swarmkv_node_addr *expect_objowner, struct swarmkv_node_addr *peer)
{
	struct key_route_entry *key_entry=NULL;
	int slot_id=-1;
	struct slot_runtime* slot_rt=NULL;
	enum key_route_result ret=NOT_FOUND;

	slot_id=get_slot_id(key, sdslen(key));
	slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	HASH_FIND(hh, slot_rt->keyroute_table, key, sdslen(key), key_entry);
	if(!key_entry && slot_rt->state==STATE_MIGRATING)
	{
		swarmkv_node_addr_copy(peer, &(slot_rt->peer.addr));
		ret=ASK;
		pthread_mutex_unlock(&slot_rt->mutex);
		return ret;
	}

	switch(op)
	{
		case KEYSPACE_GET:
			if(key_entry)
			{
				swarmkv_node_addr_copy(peer, &key_entry->owner);
				ret=FOUND;
			}
			else
			{
				ret=NOT_FOUND;
			}
			break;
		case KEYSPACE_SET:
			if(key_entry)
			{
				swarmkv_node_addr_copy(&(key_entry->owner), expect_objowner);
			}
			else
			{
				key_entry=key_entry_new(key, expect_objowner);
				HASH_ADD_KEYPTR(hh, slot_rt->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			}
			swarmkv_node_addr_copy(peer, &key_entry->owner);
			ret=FOUND;
			break;
		case KEYSPACE_SETNXGET:
			if(!key_entry)
			{
				key_entry=key_entry_new(key, expect_objowner);
				HASH_ADD_KEYPTR(hh, slot_rt->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			}
			swarmkv_node_addr_copy(peer, &key_entry->owner);
			ret=FOUND;
			break;
		case KEYSPACE_DEL:
			if(key_entry)
			{
				HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
				swarmkv_node_addr_copy(peer, &key_entry->owner);
				ret=FOUND;
				key_entry_free(key_entry);
			}
			else
			{
				ret=NOT_FOUND;
			}
			break;
		default:
			assert(0);
			break;
	}
	pthread_mutex_unlock(&slot_rt->mutex);
	return ret;
}

void keyspace_get_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	struct swarmkv_node_addr obj_owner;
	ret=keyspace_generic(ks, KEYSPACE_GET, cmd->argv[2], NULL, &obj_owner);
	*reply=make_keyspace_reply(ret, &obj_owner);
	return;
}
enum cmd_fastpath_result keyspace_get_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE GET <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	int self_is_keyowner=0;
	sds key=cmd->argv[2];
	
	struct swarmkv_node_addr peer;
	self_is_keyowner=swarmkv_keyspace_get_key_owner(ks, key, &peer);
	if(self_is_keyowner)
	{
		ret=keyspace_generic(ks, KEYSPACE_GET, key, NULL, &peer);
		*reply=make_keyspace_reply(ret, &peer);
		return ret==ASK?REDIRECT:FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &peer);
		return REDIRECT;
	}
}

void keyspace_set_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE SET <key> <host> <port>  */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	struct swarmkv_node_addr obj_owner;
	if(cmd->argc >= 5)
	{		
		sds2node_addr(cmd->argv+3, &obj_owner);
	}
	else //If object owner is not specified, let the keyowner be the object owner.
	{
		swarmkv_node_addr_copy(&obj_owner, &(ks->self.addr));
	}

	ret=keyspace_generic(ks, KEYSPACE_SET, cmd->argv[2], &obj_owner, &obj_owner);
	*reply=make_keyspace_reply(ret, &obj_owner);
	return;
}

enum cmd_fastpath_result keyspace_set_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	const sds key=cmd->argv[2];
	
	struct swarmkv_node_addr peer;
	
	sds2node_addr(cmd->argv+3, &peer);

	int self_is_keyowner=0;
	self_is_keyowner=swarmkv_keyspace_get_key_owner(ks, key, &peer);
	if(self_is_keyowner)	
	{
		ret=keyspace_generic(ks, KEYSPACE_SET, key, &peer, &peer);
		*reply=make_keyspace_reply(ret, &peer);
		return ret==ASK?REDIRECT:FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &peer);
		return REDIRECT;
	}
}

void keyspace_setnxget_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE SETNXGET key IP port */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	struct swarmkv_node_addr peer;
	
	if(cmd->argc >= 5)
	{		
		sds2node_addr(cmd->argv+3, &peer);
	}
	else //If object owner is not specified, let the keyowner be the object owner.
	{
		swarmkv_node_addr_copy(&peer, &(ks->self.addr));
	}
	ret=keyspace_generic(ks, KEYSPACE_SETNXGET, cmd->argv[2], &peer, &peer);
	*reply=make_keyspace_reply(ret, &peer);
	return;
}
enum cmd_fastpath_result keyspace_setnxget_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	const sds key=cmd->argv[2];
	
	struct swarmkv_node_addr peer;
	memset(&peer, 0, sizeof(peer));
	
	if(cmd->argc >= 5)
	{		
		sds2node_addr(cmd->argv+3, &peer);
	}
	else //If object owner is not specified, let the keyowner be the object owner.
	{
		swarmkv_node_addr_copy(&peer, &(ks->self.addr));
	}

	int self_is_keyowner=0;
	self_is_keyowner=swarmkv_keyspace_get_key_owner(ks, key, &peer);
	if(self_is_keyowner)	
	{
		ret=keyspace_generic(ks, KEYSPACE_SETNXGET, key, &peer, &peer);
		*reply=make_keyspace_reply(ret, &peer);
		return ret==ASK?REDIRECT:FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &peer);
		return REDIRECT;
	}
}

void keyspace_del_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	struct swarmkv_node_addr peer;

	ret=keyspace_generic(ks, KEYSPACE_DEL, cmd->argv[2], NULL, &peer);
	*reply=make_keyspace_reply(ret, &peer);

	return;
}
enum cmd_fastpath_result keyspace_del_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE DEL <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum key_route_result ret=NOT_FOUND;

	int self_is_keyowner=0;
	const sds key=cmd->argv[2];
	struct swarmkv_node_addr peer;

	self_is_keyowner=swarmkv_keyspace_get_key_owner(ks, key, &peer);
	if(self_is_keyowner)
	{
		ret=keyspace_generic(ks, KEYSPACE_DEL, key, NULL, &peer);
		*reply=make_keyspace_reply(ret, &peer);
		return ret==ASK?REDIRECT:FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &peer);
		return REDIRECT;
	}
	
}
UT_icd ut_swarmkv_reply_pointer_icd = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};
void keyspace_keys_command_as_owner(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *accessing_node, struct swarmkv_reply **reply)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	size_t i=0, n_matched=0;
	struct slot_runtime *slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	const sds pattern=cmd->argv[4];
	int is_matched=0;
	UT_array* matched_replies=NULL;
	struct swarmkv_reply *r=NULL;
	utarray_new(matched_replies, &ut_swarmkv_reply_pointer_icd);
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		if(!slot_rt->I_am_owner && slot_rt->state!=STATE_IMPORTING)
		{
			continue;
		}
		pthread_mutex_lock(&slot_rt->mutex);
		HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp)
		{
			is_matched=stringmatchlen(pattern, sdslen(pattern), key_entry->key, sdslen(key_entry->key), 0);
			if(is_matched)
			{
				r=swarmkv_reply_new_string(key_entry->key, sdslen(key_entry->key));
				utarray_push_back(matched_replies, &r);
			}
		}
		pthread_mutex_unlock(&slot_rt->mutex);
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
	matched_replies=NULL;
	return;
}
enum cmd_fastpath_result keyspace_keys_command_fastpath(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE KEYS IP Port key-pattern */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	struct swarmkv_node_addr asking_node;
	strncpy(asking_node.ip_addr, cmd->argv[2], sizeof(asking_node.ip_addr));
	sscanf(cmd->argv[3], "%u", &(asking_node.p2p_port));

	if(0==swarmkv_node_addr_compare(&(ks->self.addr), &asking_node))
	{
		keyspace_keys_command_as_owner(mod_keyspace, cmd, NULL, reply);
		return FINISHED;
	}
	else
	{
		*reply=make_keyspace_reply(ASK, &asking_node);
		return REDIRECT;
	}
}


