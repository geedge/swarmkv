#include "swarmkv_keyspace.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include "swarmkv_net.h"

#include "cJSON.h"
#include "sds.h"
#include "base64.h"
#include "xxhash.h"

#include <MESA/MESA_handle_logger.h>

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define	CONSUL_HOST	"127.0.0.1"
#define	CONSUL_DEFAULT_PORT	8500

#define	LOCAL_HOST	"127.0.0.1"

#define	XXHASH_SEED	5210
#define MODULE_SWARMKV_KEYSPACE		"swarmkv.keyspace"
enum slot_state
{
	slot_unkonwn,
	slot_stable,
	slot_mirgrating
};

struct key_slot
{
	int slot_id;
	int key_owner_node_id;
	struct swarmkv_node owner;
	struct swarmkv_node replica[MAX_REPLICA_NUM];
	enum slot_state state;
};

struct swarmkv_keyspace
{
	char cluster_name[SWARMKV_SYMBOL_MAX];
	pthread_t thr;
	int readable_tid;

	int node_id;	//Self node ID
	struct swarmkv_node self;
	pthread_rwlock_t rwlock_slots;
	struct key_slot slots[SWARMKV_SLOT_NUM];

	unsigned int consul_agent_port;
	unsigned int health_check_port;	//HTTP
	int dryrun;
	struct event_base * evbase;
	int init_success;

	int is_leader;
	int consul_lead_key_modify_idx;
	int consul_slots_modify_idx;
	int consul_nodes_modify_idx;
	char consul_service_id[SWARMKV_SYMBOL_MAX];
	char consul_check_id[SWARMKV_SYMBOL_MAX];
	char consul_session_id[SWARMKV_SYMBOL_MAX];

	struct evhttp* http_server;//
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

struct swarmkv_keyspace* swarmkv_keyspace_new(const char* cluster_name, struct event_base * evbase, unsigned int http_port, unsigned int p2p_port, void* logger)
{
	struct swarmkv_keyspace* ks=ALLOC(struct swarmkv_keyspace, 1);
	ks->health_check_port=http_port;
	ks->p2p_port=p2p_port;
	strncpy(ks->cluster_name, cluster_name, sizeof(ks->cluster_name));
	ks->consul_agent_port=CONSUL_DEFAULT_PORT;
    ks->evbase = evbase;
	ks->logger = logger;
	
	return ks;
}
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

int consul_service_register(struct swarmkv_keyspace* ks)
{
	int ret=0;
	char health_check_url[SWARMKV_URL_MAX]="";
	cJSON* service=cJSON_CreateObject();
	cJSON_AddStringToObject(service, "Name", ks->cluster_name);
	snprintf(ks->consul_service_id, sizeof(ks->consul_service_id), "%s-%d", ks->cluster_name, ks->health_check_port);
	cJSON_AddStringToObject(service, "ID", ks->consul_service_id);
	cJSON_AddStringToObject(service, "Address", ks->self.ip_addr);
	cJSON_AddNumberToObject(service, "Port", ks->p2p_port);

	cJSON* check=cJSON_CreateObject();

	cJSON_AddStringToObject(check, "Name", ks->cluster_name);
	snprintf(ks->consul_check_id, sizeof(ks->consul_check_id), "%s-%d", ks->cluster_name, ks->health_check_port);
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
	printf("%s\n", req_body);
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

void consul_create_session_async(struct swarmkv_keyspace* ks)
{
	cJSON* session=NULL, *service_check_array=NULL, *service_check=NULL, *check_array=NULL, *session_create_response=NULL, *session_id=NULL;
	session=cJSON_CreateObject();
	cJSON_AddStringToObject(session, "LockDelay", "5s");
	cJSON_AddStringToObject(session, "Name", "swarmkv-leader-election-lock");
	cJSON_AddStringToObject(session, "Behavior", "release");
//	snprintf(ttl, sizeof(ttl), "%ds", CONSUL_SESSION_TIMEOUT_SECONDS);
//	cJSON_AddStringToObject(session, "TTL", ttl);

	service_check=cJSON_CreateObject();
	cJSON_AddStringToObject(service_check, "ID", ks->consul_check_id);
	service_check_array=cJSON_CreateArray();
	cJSON_AddItemToArray(service_check_array, service_check);
	cJSON_AddItemToObject(session, "ServiceChecks", service_check_array);

/*
	char* p[1];
	p[0]=ks->cluster_name;
	check_array=cJSON_CreateStringArray(p, 1);
	cJSON_AddItemToObject(session, "Checks", check_array);
	
*/
	char* payload=NULL;
	payload=cJSON_Print(session);
	sds req_body=sdsnew(payload);
	free(payload);
	printf("%s\n", req_body);

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
			sds out=sdsnewlen(SDS_NOINIT, len);
			evbuffer_copyout(buf, out, len);

			if(0==strncmp(out, "true", strlen("true")))
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
		}
		else
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "run for leader failed: HTTP Code %d.", resp_code);
		}
	}
	consul_watch_leadership_changes_async(ks);
}
sds self_print_json(struct swarmkv_keyspace* ks)
{
	cJSON* node_info=NULL;
	node_info=cJSON_CreateObject();
	cJSON_AddStringToObject(node_info, "ip", ks->self_ip);
	cJSON_AddNumberToObject(node_info, "p2p_port", ks->p2p_port);
	char* payload=NULL;
	payload=cJSON_Print(node_info);

	sds self_json=NULL;
	self_json=sdsnew(payload);
	free(payload);
	return self_json;
}
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks)
{
	sds req_body=self_print_json(ks);

	char url[SWARMKV_URL_MAX];
	
	//run for leader
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?acquire=%s", ks->cluster_name, ks->consul_session_id);

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
			sds out=sdsnewlen(SDS_NOINIT, len);
			evbuffer_copyout(buf, out, len);
			metadata_array=cJSON_Parse(out);
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
			sdsfree(out);
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
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?index=%d&wait=10s", ks->cluster_name, ks->consul_lead_key_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	return;
}
void __watch_slots_changes_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	cJSON *metadata_array=NULL, *metadata=NULL, *value=NULL, *modify_idx=NULL;
	cJSON *slot_table=NULL, *range=NULL, *slot=NULL, *ip=NULL, *port=NULL;
	int resp_code=0;
	int	i=0, j=0, n_record=0, n_slot=0;
	int range_start=0, range_end=0;
	struct key_slot key_slots[SWARMKV_SLOT_NUM];
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
		sds out=sdsnewlen(NULL, len);
		evbuffer_copyout(buf, out, len);
		metadata_array=cJSON_Parse(out);
		metadata=cJSON_GetArrayItem(metadata_array, 0);
		modify_idx=cJSON_GetObjectItem(metadata, "ModifyIndex");
		ks->consul_slots_modify_idx=modify_idx->valueint;
		value=cJSON_GetObjectItem(metadata, "Value");
		sds decode_buffer=sdsnewlen(NULL, BASE64_DECODE_OUT_SIZE(strlen(value->string)));
		base64_decode(value->string, strlen(value->string), (unsigned char*)decode_buffer);
		
		slot_table=cJSON_Parse(decode_buffer);
		n_record=cJSON_GetArraySize(slot_table);
		for(i=0; i<n_record; i++)
		{
			slot=cJSON_GetArrayItem(slot_table, i);
			range=cJSON_GetObjectItem(slot, "slot");
			if(strstr(slot->string, "-"))
			{
				sscanf(range->string, "%d-%d", &range_start, &range_end);
			}
			else
			{
				sscanf(range->string, "%d", &range_start);
				range_end=range_start;
			}
			ip=cJSON_GetObjectItem(slot, "ip");	
			port=cJSON_GetObjectItem(slot, "p2p_port");
			for(j=range_start; j<range_end+1; j++)
			{
				strncpy(key_slots[n_slot].owner.ip_addr, ip->string, sizeof(key_slots[n_slot].owner.ip_addr));
				key_slots[n_slot].owner.p2p_port= (unsigned int)port->valueint;
				n_slot++;
			}
		}
		cJSON_free(metadata_array);
		cJSON_free(slot_table);
		sdsfree(decode_buffer);
		sdsfree(out);
	}
	assert(n_slot==0||n_slot==SWARMKV_SLOT_NUM);
	if(n_slot==SWARMKV_SLOT_NUM)
	{
		pthread_rwlock_wrlock(&ks->rwlock_slots);
		memcpy(ks->slots, key_slots, sizeof(ks->slots));
		pthread_rwlock_unlock(&ks->rwlock_slots);
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "key slots are updated.");
	}
	consul_watch_slots_changes_async(ks);
}
void consul_watch_slots_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	struct evhttp_request* request=evhttp_request_new(__watch_slots_changes_cb, ks);
	struct evhttp_connection* conn=evhttp_connection_base_new(ks->evbase, NULL, CONSUL_HOST, ks->consul_agent_port);	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);
	evhttp_connection_set_timeout(conn, 1800);

	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?index=%d", ks->cluster_name, ks->consul_slots_modify_idx);
    evhttp_add_header(output_headers, "Host", LOCAL_HOST);
	evhttp_add_header(output_headers, "Connection", "close");

	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	return;
}
void update_key_slots(struct swarmkv_keyspace* ks, struct swarmkv_node* nodes, int n_node)
{
	return;
}
void __watch_nodes_changes_cb(struct evhttp_request *req, void *arg)
{
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	int resp_code=0;
	int i=0, n_record=0;
	const char* tmp=NULL;
	cJSON *health_reponse=NULL, *record=NULL, *item=NULL, *service=NULL;
	struct swarmkv_node nodes[SWARMKV_NODE_MAX];
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
		sds out=sdsnewlen(NULL, len);
		evbuffer_copyout(buf, out, len);
		health_reponse=cJSON_Parse(out);
		n_record=cJSON_GetArraySize(health_reponse);
		for(i=0; i<n_record; i++)
		{
			record=cJSON_GetArrayItem(health_reponse, i);
			service=cJSON_GetObjectItem(record, "Service");
			item=cJSON_GetObjectItem(service, "Address");
			strncpy(nodes[i].ip_addr, item->valuestring, sizeof(nodes[i].ip_addr));
			item=cJSON_GetObjectItem(service, "Port");
			nodes[i].p2p_port=(unsigned int)item->valueint;
		}
		cJSON_free(health_reponse);
		sdsfree(out);
		struct evkeyvalq *input_headers=evhttp_request_get_input_headers(req);	
		tmp=evhttp_find_header(input_headers, "X-Consul-Index");
		sscanf(tmp, "%d", &ks->consul_nodes_modify_idx);
	}
	if(ks->is_leader)
	{
		update_key_slots(ks, nodes, i);
		consul_watch_nodes_changes_async(ks);
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

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1&index=%d", ks->cluster_name, ks->consul_nodes_modify_idx);
	evhttp_make_request(conn, request, EVHTTP_REQ_GET, url);
	
}
static sds keyslots_print_json(struct key_slot slots[], size_t n_slot)
{
	cJSON *slot_array=cJSON_CreateArray();
	sds printed=NULL;
	cJSON *slot=NULL, *node=NULL, *replica_array=NULL;
	char range[256]="";
	size_t i=0, j=0;
	int start=0, end=0;
	start=end=slots[0].slot_id;
	for(i=0; i<n_slot+1; i++)
	{
		if(i<n_slot
			&& 0==memcmp(&(slots[i].owner), &(slots[end].owner), sizeof(slots[i].owner))
			&& 0==memcmp(&(slots[i].replica), &(slots[end].replica), sizeof(slots[i].replica)) )
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
		cJSON_AddStringToObject(node, "ip", slots[end].owner.ip_addr);
		cJSON_AddNumberToObject(node, "port", slots[end].owner.p2p_port);
		cJSON_AddItemToObject(slot, "owner", node);

		replica_array=cJSON_CreateArray();
		for(j=0; j<MAX_REPLICA_NUM; j++)
		{
			if(0==strlen(slots[end].replica[j].ip_addr))
			{
				break;
			}
			node=cJSON_CreateObject();
			cJSON_AddStringToObject(node, "ip", slots[end].replica[j].ip_addr);
			cJSON_AddNumberToObject(node, "port", slots[end].replica[j].p2p_port);
			cJSON_AddItemToArray(replica_array, node);
		}
		if(j==0)
		{
			cJSON_Delete(replica_array);
		}
		else
		{
			cJSON_AddItemToObject(slot, "replicas", replica_array);
		}
		cJSON_AddItemToArray(slot_array, slot);
		start=end=i;
	}
	char* payload=NULL;
	payload=cJSON_Print(slot_array);
	printf("%s\n", payload);
	printed=sdsnew(payload);
	free(payload);
	cJSON_Delete(slot_array);
	return printed;
}
void keyslots_init(struct swarmkv_keyspace* ks)
{
	int i=0;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		ks->slots[i].slot_id=i;
		strncpy(ks->slots[i].owner.ip_addr, ks->self_ip, sizeof(ks->slots[i].owner.ip_addr));
		ks->slots[i].owner.p2p_port=ks->p2p_port;
	}
	return;
}
static void consul_kv_init_if_nonexist(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	int resp_code_get=0, resp_code_put=0;
	sds slot_table=NULL, self_info=NULL;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->cluster_name);
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
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead", ks->cluster_name);

	resp_code_get=http_blocking_request(EVHTTP_REQ_GET, CONSUL_HOST, ks->consul_agent_port, url, NULL, NULL);
	if(resp_code_get==404)
	{
		self_info=self_print_json(ks);
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

int swarmkv_keyspace_init(struct swarmkv_keyspace* ks)
{
	int ret=-1;
	if(0==strlen(ks->self_ip))
	{
		ret=consul_get_self_ip(ks);
		if(ret<0)
		{
			MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "cannot get self IP from local consul agent.");		
			goto error_out;
		}
	}
	keyslots_init(ks);
	consul_kv_init_if_nonexist(ks);
	ks->http_server=http_server_new(ks->evbase, ks->health_check_port, ks->logger);
	if(!ks->http_server)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "HTTP server start failed");
		goto error_out;
	}
	consul_watch_slots_changes_async(ks);
	if(ks->dryrun)
	{
		MESA_handle_runtime_log(ks->logger, RLOG_LV_INFO, MODULE_SWARMKV_KEYSPACE, "work on dry run mode.");
		return 0;
	}
	ret=consul_service_register(ks);
	if(ret<0)
	{	
		MESA_handle_runtime_log(ks->logger, RLOG_LV_FATAL, MODULE_SWARMKV_KEYSPACE, "consul service register failed.");
		goto error_out;
	}
	consul_watch_leadership_changes_async(ks);
	
	return 0;
error_out:
	return -1;
}

int swarmkv_keyspace_set_consul_port(struct swarmkv_keyspace* ks, unsigned int consul_port)
{
	ks->consul_agent_port=consul_port;
	return 0;
}
int swarmkv_keyspace_set_dryrun(struct swarmkv_keyspace* ks)
{
	ks->dryrun=1;
	return 0;
}
int swarmkv_keyspace_set_selfip(struct swarmkv_keyspace* ks, const char* ip)
{
	strcpy(ks->self_ip, ip);
	return 0;
}
void swarmkv_keyspace_free(struct swarmkv_keyspace* ks)
{
	if(ks->http_server)
	{
		evhttp_free(ks->http_server);
		ks->http_server=NULL;
	}
	free(ks);
	return;
}
//
//return 1 when self is key owner;
//return 0 when key is owned by someone else
int swarmkv_keyspace_get_keyowner(struct swarmkv_keyspace* ks, const char* key, size_t keylen, struct swarmkv_node* keyowner)
{
	XXH32_hash_t hash = XXH32(key, keylen, XXHASH_SEED);
	int slot_id=hash%SWARMKV_SLOT_NUM;
	pthread_rwlock_rdlock(&ks->rwlock_slots);
	*keyowner=ks->slots[slot_id];
	pthread_rwlock_unlock(&ks->rwlock_slots);
	if(0==memcmp(keyowner, &(ks->self), sizeof(ks->self)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
