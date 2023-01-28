#include "swarmkv_keyspace.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include "swarmkv_monitor.h"
#include "swarmkv_error.h"

#include "cJSON.h"
#include "sds.h"
#include "mpack.h"
#include "base64.h"
#include "utarray.h"
#include "uthash.h"
#include "utlist.h"
#include "timeout.h"
#include "log.h"

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


//#define	CONSUL_HOST	"127.0.0.1"
#define	CONSUL_HOST	"localhost"
#define	CONSUL_DEFAULT_PORT	8500

#define	LOCAL_HOST	"127.0.0.1"


#define MODULE_SWARMKV_KEYSPACE		module_name_str("swarmkv.keyspace")


struct replica_node
{
	node_t node;
	UT_hash_handle hh;
};
struct key_route_entry
{
	sds key;	
	struct replica_node *hash_replica;
//	struct replica_node *first_replica;//a reference of the first replica in hash_replica;
	uint64_t abs_timeout;
	char is_expiring;
	struct timeout timeout_handle;
	UT_hash_handle hh;
};
struct key_route_entry *key_entry_new_len(const char* key, size_t key_len)
{
	struct key_route_entry *entry=NULL;
	entry=ALLOC(struct key_route_entry, 1);
	entry->key=sdsnewlen(key, key_len);	
	return entry;

}
struct key_route_entry *key_entry_new(const sds key)
{
	struct key_route_entry *entry=NULL;
	entry=key_entry_new_len(key, sdslen(key));
	return entry;
}

void key_entry_free(struct key_route_entry *key_entry)
{
	sdsfree(key_entry->key);
	key_entry->key=NULL;
	struct replica_node *replica=NULL, *tmp=NULL;
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		HASH_DELETE(hh, key_entry->hash_replica, replica);
		free(replica);
	}
	free(key_entry);
}
int key_entry_add_replica_node(struct key_route_entry *key_entry, const node_t *node)
{
	struct replica_node  *tmp=NULL;
	HASH_FIND(hh, key_entry->hash_replica, node, sizeof(node_t), tmp);
	if(tmp)
	{
		return 0;
	}
	tmp=ALLOC(struct replica_node, 1);
	node_copy(&tmp->node, node);
	HASH_ADD(hh, key_entry->hash_replica, node, sizeof(node_t), tmp);
	return 1;
}

int key_entry_rdel_replica_node(struct key_route_entry *key_entry, const node_t *node)
{
	struct replica_node *found=NULL;

	HASH_FIND(hh, key_entry->hash_replica, node, sizeof(node_t), found);
	if(found)
	{
		HASH_DEL(key_entry->hash_replica, found);
		return 1;
	}
	else
	{
		return 0;
	}
}

struct swarmkv_reply *key_entry_list_replica_nodes(struct key_route_entry *key_entry)
{
	struct swarmkv_reply *reply=NULL;
	size_t n_existed_replica=0, i=0;
	struct replica_node *replica=NULL, *tmp=NULL;

	n_existed_replica=HASH_COUNT(key_entry->hash_replica);
	reply=swarmkv_reply_new_array(n_existed_replica);
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		reply->elements[i]=swarmkv_reply_new_node(&replica->node, 0);
		i++;
	}
	return reply;
}
struct crdt_del_ctx
{
	struct future *f;
	node_t peer;
	sds key;
};
void crdt_del_ctx_free(struct crdt_del_ctx *ctx)
{
	future_destroy(ctx->f);
	sdsfree(ctx->key);
	free(ctx);
}

static void crdt_del_on_succ(void* result, void* user)
{
	
	struct crdt_del_ctx *ctx = (struct crdt_del_ctx*)user;
	const struct swarmkv_reply* reply=(const struct swarmkv_reply*) result;
//	assert(reply->type==SWARMKV_REPLY_INTEGER);
//	assert(reply->integer==1);
	if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer !=1)
	{
//		printf("crdt del %s at %s:%u failed.\n", ctx->key, ctx->peer.ip_addr, ctx->peer.cluster_port);
	}
	crdt_del_ctx_free(ctx);
	return;
}
static void crdt_del_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct crdt_del_ctx *ctx = (struct crdt_del_ctx*)user;
	crdt_del_ctx_free(ctx);
	return;
}

void key_entry_deletion_notification(struct key_route_entry *key_entry, exec_cmd_func *exec_cmd, struct swarmkv *exec_cmd_handle)
{
	struct replica_node *replica=NULL, *tmp=NULL;
	struct swarmkv_cmd *crdt_del_cmd=swarmkv_cmd_new(3);
	crdt_del_cmd->argv[0]=sdsnew("crdt");
	crdt_del_cmd->argv[1]=sdsnew("del");
	crdt_del_cmd->argv[2]=sdsdup(key_entry->key);
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		struct crdt_del_ctx *ctx=ALLOC(struct crdt_del_ctx, 1);
		node_copy(&ctx->peer, &replica->node);
		ctx->key=sdsdup(key_entry->key);
		ctx->f=future_create("key_del", crdt_del_on_succ, crdt_del_on_fail, ctx);
		exec_cmd(exec_cmd_handle, crdt_del_cmd, NULL, &replica->node, ctx->f);
	}
	swarmkv_cmd_free(crdt_del_cmd);
	return;
}

struct slot_runtime
{
	struct key_slot slot;
	enum slot_state state;
	node_t rebalancing_peer;//rebalancing_peer is used for state MIGRATING and IMPORTING

	struct key_route_entry *keyroute_table;
	struct timeouts *expires;

	int I_am_owner;
	pthread_mutex_t mutex;
};
void http_connection_close_callback(struct evhttp_connection *conn, void *arg)
{
//	struct consul_task *task=(struct consul_task *)arg;
	//printf("http connection %s %p closed\n", task->task_name, conn);
	return;
}

struct swarmkv_keyspace
{
	struct swarmkv_module module;
	const struct swarmkv_options *opts;
	char db_name[SWARMKV_SYMBOL_MAX];
	pthread_t thr;
	int readable_tid;
	node_t self;
	uuid_t uuid;
	pthread_rwlock_t rwlock;	
	struct slot_runtime slot_rts[KEYSPACE_SLOT_NUM];
	node_t nodes[SWARMKV_MAX_NODE_NUM];

	char consul_agent_host[MAX_IPV4_ADDR_LEN];
	unsigned int consul_agent_port;
	int dryrun;
	int init_success;

	int is_leader;
	int consul_lead_key_modify_idx;
	int consul_slots_modify_idx;
	int consul_nodes_modify_idx;
	char consul_service_id[SWARMKV_SYMBOL_MAX];
	char consul_check_id[SWARMKV_SYMBOL_MAX];
	char consul_session_id[SWARMKV_SYMBOL_MAX];

	struct evhttp *http_server;
	struct event_base *evbase;	
	struct consul_client *consul_client;

	//reference, no need free	
	exec_cmd_func *exec_cmd_func;
	struct swarmkv *exec_cmd_handle;
	struct swarmkv_module *mod_monitor;
	struct log_handle *logger;

};
void http_request_on_close(struct evhttp_connection *conn, void *arg)
{
	return;
}

struct consul_client;
struct consul_request
{
	char symbol[SWARMKV_SYMBOL_MAX];
	long long request_id;
	struct evhttp_request *evhttpreq;
	struct future *f;
	struct consul_client *ref_client;
	UT_hash_handle hh;
};
struct consul_client
{
	unsigned short consul_agent_port;
	char consul_agent_host[32];
	long long req_id_generator;
	struct event_base *evbase;	//reference of keyspace evbase
	struct log_handle *logger;	//reference of logger
	struct evhttp_connection *evhttpconn; //tcp connection for the request
	struct consul_request *request_table;
};
void consul_request_callback(struct evhttp_request *evhttpreq, void *arg);
struct consul_request *consul_request_new(struct consul_client *client, const char *symbol, struct future *f)
{
	struct consul_request *req=ALLOC(struct consul_request, 1);
	
	strncpy(req->symbol, symbol, sizeof(req->symbol));
	req->evhttpreq=evhttp_request_new(consul_request_callback, req);
	req->f=f;
	req->ref_client=client;
	req->request_id=client->req_id_generator++;

	struct consul_request *tmp=NULL;
	HASH_FIND(hh, client->request_table, symbol, strlen(symbol), tmp);
	assert(tmp==NULL);

	HASH_ADD(hh, client->request_table, symbol, strlen(symbol), req);
	
	return req;
}
void consul_request_free(struct consul_request *req)
{
	//evhttp_connection_free(req->evhttpconn);
	//if(req->evhttpreq) evhttp_request_free(req->evhttpreq);
	future_destroy(req->f);
	
	free(req);
}
void consul_request_callback(struct evhttp_request *evhttpreq, void *arg)
{
	struct consul_request *req=(struct consul_request *)arg;
	
	struct promise *p=future_to_promise(req->f);
	HASH_DEL(req->ref_client->request_table, req);
	if(!evhttpreq || !evhttp_request_get_response_code(evhttpreq))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		log_fatal(req->ref_client->logger, MODULE_SWARMKV_KEYSPACE, "%s failed: %s",
								req->symbol,
								evutil_socket_error_to_string(errcode));
		sleep(1000);
		promise_failed(p, FUTURE_ERROR_EXCEPTION, "socket error");		
	}

	promise_success(p, evhttpreq);
	
	//The request should be freed automatically after the http_request_done has finished executing.
	req->evhttpreq=NULL;
	consul_request_free(req);
	return;
}
void consul_request_make(struct consul_request *req, enum evhttp_cmd_type cmd, const char *url)
{
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req->evhttpreq);
	evhttp_add_header(output_headers, "Host", req->ref_client->consul_agent_host);
//	evhttp_add_header(output_headers, "Connection", "close");
	
	evhttp_make_request(req->ref_client->evhttpconn, req->evhttpreq, cmd, url);
}

struct consul_client *consul_client_new(const char *host, unsigned short port, struct event_base *evbase, struct log_handle *logger)
{
	struct consul_client *client=ALLOC(struct consul_client, 1);
	client->consul_agent_port=port;
	strncpy(client->consul_agent_host, host, sizeof(client->consul_agent_host));
	client->logger=logger;
	client->evbase=evbase;
	client->evhttpconn=evhttp_connection_base_new(client->evbase, NULL, client->consul_agent_host, client->consul_agent_port);

	evhttp_connection_set_timeout(client->evhttpconn, 1800);//set to 30min for blocking query.
	evhttp_connection_set_closecb(client->evhttpconn, http_request_on_close, client);
	return client;
}
void consul_client_free(struct consul_client *client)
{
	struct consul_request *req=NULL, *tmp=NULL;
	HASH_ITER(hh, client->request_table, req, tmp)
	{
		HASH_DEL(client->request_table, req);
		consul_request_free(req);
	}
	evhttp_connection_free(client->evhttpconn);
	free(client);
}
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks);
void consul_watch_leadership_changes_async(struct swarmkv_keyspace* ks);
void consul_watch_nodes_changes_async(struct swarmkv_keyspace* ks);
void consul_watch_slots_changes_async(struct swarmkv_keyspace* ks);
void keyspace_active_expire_cycle(struct swarmkv_keyspace *ks);

void health_check_cb(struct evhttp_request *request, void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	struct evbuffer* evbuf_reply = evbuffer_new();
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);

	 //HTTP header
	evhttp_add_header(output_headers, "Server", "Swarmkv v3.0");
	evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=UTF-8");//utf8
	evhttp_add_header(output_headers, "Connection", "close");
	
	evbuffer_add_printf(evbuf_reply, "SwarmKV node of cluster %s is running", ks->db_name);
	evhttp_send_reply(request, HTTP_OK, "OK", evbuf_reply);
	evbuffer_free(evbuf_reply);
	return;
}

struct evhttp* http_server_new(struct event_base* evbase, unsigned int *port_listen, void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	void *logger=ks->logger;
    struct evhttp* http = evhttp_new(evbase);
	struct evhttp_bound_socket *bound_socket=NULL;
	bound_socket=evhttp_bind_socket_with_handle(http, "0.0.0.0", *port_listen);
	if(bound_socket==NULL)
    {
		log_fatal(logger, MODULE_SWARMKV_KEYSPACE, "evhttp_bind_socket on port %u failed.", port_listen);
		return NULL;
    }
	evutil_socket_t sockfd=-1;
	sockfd=evhttp_bound_socket_get_fd(bound_socket);
	evutil_make_socket_closeonexec(sockfd);
    struct sockaddr_storage ss;
	ev_socklen_t socklen = sizeof(ss);
	if (getsockname(sockfd, (struct sockaddr *)&ss, &socklen) < 0)
	{
		log_fatal(logger, MODULE_SWARMKV_KEYSPACE,  "getsockname from evhttp fd failed.");
	}
	if(*port_listen==0)
	{
		*port_listen=ntohs(((struct sockaddr_in*)&ss)->sin_port);
	}
    int http_option_timeout = 5; //in seconds
    evhttp_set_timeout(http, http_option_timeout);
    evhttp_set_allowed_methods( http , EVHTTP_REQ_GET);
    //Set a callback for a specified URI
    evhttp_set_cb(http, "/health", health_check_cb, ks);
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
	snprintf(ks->consul_service_id, sizeof(ks->consul_service_id), "%.128s-%d", ks->db_name, ks->opts->cluster_announce_port);
	cJSON_AddStringToObject(service, "ID", ks->consul_service_id);
	cJSON_AddStringToObject(service, "Address", ks->opts->cluster_announce_ip);
	cJSON_AddNumberToObject(service, "Port", ks->opts->cluster_announce_port);

	cJSON* meta=cJSON_CreateObject();
	uuid_unparse_lower(ks->uuid, uuid_str);
	cJSON_AddStringToObject(meta, "node_uuid", uuid_str);
	
	cJSON_AddItemToObject(service, "Meta", meta);

	cJSON* check=cJSON_CreateObject();

	cJSON_AddStringToObject(check, "Name", ks->db_name);
	snprintf(ks->consul_check_id, sizeof(ks->consul_check_id), "%.128s-%u", ks->db_name, ks->opts->cluster_announce_port);
	cJSON_AddStringToObject(check, "CheckID", ks->consul_check_id);
	snprintf(health_check_url, sizeof(health_check_url), "http://%s:%u/health", ks->opts->cluster_announce_ip, ks->opts->health_check_announce_port);
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
	resp_code=http_blocking_request(EVHTTP_REQ_PUT, ks->consul_agent_host, ks->consul_agent_port, "/v1/agent/service/register?replace-existing-checks=true",
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
static void __ks_dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	keyspace_active_expire_cycle(ks);
	return;
}

void *swarmkv_keyspace_thread(void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv-ks");
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);
	struct event * ev = event_new(ks->evbase, -1, EV_PERSIST, __ks_dummy_event_handler, ks);
	struct timeval timer_delay = {2, 0};
	evtimer_add(ev, &timer_delay);
	event_base_dispatch(ks->evbase);
	event_del(ev);
	event_free(ev);

	return NULL;
}
void consul_session_create_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct swarmkv_keyspace* ks = (struct swarmkv_keyspace *)arg;
	int resp_code=0;
	cJSON* session_id=NULL, *session_create_response=NULL;
	
	int is_running_for_leader=0;

	struct evbuffer *buf=NULL;
	buf=evhttp_request_get_input_buffer(req);
	size_t len = evbuffer_get_length(buf);
	sds out=sdsnewlen(SDS_NOINIT, len);
	evbuffer_copyout(buf, out, len);

	resp_code=evhttp_request_get_response_code(req);	
	if(resp_code==200)
	{

		
		session_create_response=cJSON_Parse(out);
		session_id=cJSON_GetObjectItem(session_create_response, "ID");
		strncpy(ks->consul_session_id, session_id->valuestring, sizeof(ks->consul_session_id));
		cJSON_Delete(session_create_response);
		consul_acquire_session_lock_async(ks);

		is_running_for_leader=1;
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "session %s is created.", ks->consul_session_id);
	}
	else
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "session creation failed: HTTP code %d, %s.",
			resp_code,
			out);
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
	sdsfree(out);
	return;
}
void consul_session_create_on_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session create failed: %s.", what);
	return;
}
void consul_create_session_async(struct swarmkv_keyspace *ks)
{
	cJSON *session=NULL, *service_check_array=NULL, *service_check=NULL;
	session=cJSON_CreateObject();
	cJSON_AddStringToObject(session, "LockDelay", "5s");
	cJSON_AddStringToObject(session, "Name", "swarmkv-leader-election-lock");
	cJSON_AddStringToObject(session, "Behavior", "release");

	service_check=cJSON_CreateObject();
	cJSON_AddStringToObject(service_check, "ID", ks->consul_check_id);
	service_check_array=cJSON_CreateArray();
	cJSON_AddItemToArray(service_check_array, service_check);
	cJSON_AddItemToObject(session, "ServiceChecks", service_check_array);

	char *payload=NULL;
	payload=cJSON_Print(session);
	cJSON_Delete(session);

	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/create");

	struct future *f=future_create(__func__, consul_session_create_on_success, consul_session_create_on_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);

	
//	evhttp_connection_set_timeout(req->evhttpconn, 2);

	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req->evhttpreq);
	
	struct evbuffer *output_buffer = evhttp_request_get_output_buffer(req->evhttpreq);
	evbuffer_add(output_buffer, payload, strlen(payload));
	char number[64];
	snprintf(number, sizeof(number), "%zu", strlen(payload));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	consul_request_make(req, EVHTTP_REQ_PUT, url);
	free(payload);
	return;

}
void consul_session_check_on_success(void *result, void *arg)
{
	int resp_code=0;
	cJSON *session_check=NULL;
	int session_check_array_size=0;
	struct swarmkv_keyspace *ks = (struct swarmkv_keyspace *)arg;
	int is_running_for_leader=0;
	struct evhttp_request *req=(struct evhttp_request *)result;

	resp_code=evhttp_request_get_response_code(req);

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
			log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "session %s is not existed, start to create a new session.", ks->consul_session_id);
			memset(ks->consul_session_id, 0, sizeof(ks->consul_session_id));
		}
		else
		{
			consul_acquire_session_lock_async(ks);
			log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "session %s is valid, run for leader.", ks->consul_session_id);
		}
		is_running_for_leader=1;
	}
	else
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "session check failed: HTTP code %d.", resp_code);
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
	return;
}
void consul_session_check_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session check failed: %s.", what);
	return;
}
void consul_session_check_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/info/%s", ks->consul_session_id);
	
	struct future *f=future_create(__func__, consul_session_check_on_success, consul_session_check_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);
	consul_request_make(req, EVHTTP_REQ_GET, url);
	return;

}
void acquire_session_lock_on_success(void* result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct swarmkv_keyspace* ks=(struct swarmkv_keyspace*)arg;
	int resp_code=0;

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
			log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "become cluster leader.");
		}
		else
		{
			log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "lose the election.");
			ks->is_leader=0;
		}
		sdsfree(resp_body_buff);
	}
	else
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "run for leader failed: HTTP Code %d.", resp_code);
	}
	consul_watch_leadership_changes_async(ks);
}
void acquire_session_lock_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session lock aquire failed: %s.", what);
	consul_watch_leadership_changes_async(ks);
	return;
}
void consul_acquire_session_lock_async(struct swarmkv_keyspace* ks)
{
	sds req_body=node_print_json(&ks->self, ks->uuid);

	char url[SWARMKV_URL_MAX];
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?acquire=%s", ks->db_name, ks->consul_session_id);

	struct future *f=future_create(__func__, acquire_session_lock_on_success, acquire_session_lock_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);

//	evhttp_connection_set_timeout(req->evhttpconn, 2);

	struct evbuffer* output_buffer = evhttp_request_get_output_buffer(req->evhttpreq);
	evbuffer_add(output_buffer, req_body, sdslen(req_body));


	char number[64];
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req->evhttpreq);
	snprintf(number, sizeof(number), "%zu", sdslen(req_body));

	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	consul_request_make(req, EVHTTP_REQ_PUT, url);
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
void watch_leadership_changes_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request*)result;
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	cJSON *metadata_array=NULL, *metadata=NULL, *modify_idx=NULL, *session=NULL;
	int resp_code=0;
	int is_running_for_leader=0;

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
			log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "cluster losts leader, start to run for leader.");
			consul_run_for_leader_async(ks);
			is_running_for_leader=1;
		}
		if(!session || 0!=strcmp(session->valuestring, ks->consul_session_id))
		{
			if(ks->is_leader)
			{
				log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "node is no longer a leader.");
				ks->is_leader=0;
			}
		}
		cJSON_Delete(metadata_array);
		sdsfree(resp_body_buff);
	}
	else if(resp_code==404)
	{
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "no leader key, start to run for leader.");
		consul_run_for_leader_async(ks);
		is_running_for_leader=1;
	}
	else
	{
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: HTTP code %d.", resp_code);
	}
	
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(ks);
	}
}
void watch_leadership_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: %s.", what);
	return;
}
void consul_watch_leadership_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?index=%d&wait=10s", ks->db_name, ks->consul_lead_key_modify_idx);

	struct future *f=future_create(__func__, watch_leadership_changes_on_success, watch_leadership_changes_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);
	consul_request_make(req, EVHTTP_REQ_GET, url);
	return;
}
void watch_slots_changes_on_success(void *result, void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	cJSON *metadata_array=NULL, *metadata=NULL, *value=NULL, *modify_idx=NULL;
	int	i=0;
	
	struct key_slot *new_slots=NULL;
	struct evhttp_request *req=(struct evhttp_request*)result;
	struct evbuffer *buf=NULL;
	buf=evhttp_request_get_input_buffer(req);
	size_t len = evbuffer_get_length(buf);
	sds resp_body_buff=sdsnewlen(SDS_NOINIT, len);
	evbuffer_copyout(buf, resp_body_buff, len);


	metadata_array=cJSON_Parse(resp_body_buff);
	metadata=cJSON_GetArrayItem(metadata_array, 0);
	modify_idx=cJSON_GetObjectItem(metadata, "ModifyIndex");
	ks->consul_slots_modify_idx=modify_idx->valueint;
	value=cJSON_GetObjectItem(metadata, "Value");
	sds decode_buffer=sdsnewlen(NULL, BASE64_DECODE_OUT_SIZE(strlen(value->valuestring)));
	base64_decode(value->valuestring, strlen(value->valuestring), (unsigned char*)decode_buffer);

	new_slots=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	json2keyslots(decode_buffer, new_slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	
	sdsfree(decode_buffer);
	sdsfree(resp_body_buff);

	struct key_slot *slot=NULL;
	struct slot_runtime *slot_rt=NULL;
	node_t *owner=NULL;
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots update started.");
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;	
		slot=&(slot_rt->slot);
		owner=&(slot->owner);
		pthread_mutex_lock(&slot_rt->mutex);
		if(0!=node_compare(owner, &(new_slots[i].owner)))
		{		
			node_copy(owner, &(new_slots[i].owner));
			if(slot_rt->state!=STATE_STABLE)
			{
//				log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "slot %d is reset to STABLE state, owner %s:%u.",
//					slot->slot_id, owner->addr.ip_addr, owner->addr.cluster_port);
				memset(&(slot_rt->rebalancing_peer), 0, sizeof(slot_rt->rebalancing_peer));
			}
			slot_rt->state=STATE_STABLE;
		}
		if(0==node_compare(owner, &ks->self))
		{
			slot_rt->I_am_owner=1;
		}
		else
		{
			slot_rt->I_am_owner=0;
		}
		pthread_mutex_unlock(&slot_rt->mutex);
	}
	log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots update finished.");
	cJSON_Delete(metadata_array);
	free(new_slots);
	consul_watch_slots_changes_async(ks);
	return;
}
void watch_slots_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	consul_watch_slots_changes_async(ks);
}
void consul_watch_slots_changes_async(struct swarmkv_keyspace *ks)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?index=%d", ks->db_name, ks->consul_slots_modify_idx);
	struct future *f=future_create(__func__, watch_slots_changes_on_success, watch_slots_changes_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);
	consul_request_make(req, EVHTTP_REQ_GET, url);
	return;
}
void propagate_slot_table_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;	
	int resp_code=0;

	resp_code=evhttp_request_get_response_code(req);	
	if(resp_code==200)
	{
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate success.");
	}
	else
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed.");
	}
	return;
}
void propagate_slot_table_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed: %s",
			what);
}
void propagate_slot_table_async(struct swarmkv_keyspace *ks, struct key_slot new_slot[], size_t slot_num)
{
	sds new_slots_json=NULL;
	new_slots_json=keyslots2json(new_slot, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", ks->db_name);
	struct future *f=future_create(__func__, propagate_slot_table_on_success, propagate_slot_table_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);

//	evhttp_connection_set_timeout(req->evhttpconn, 20);
	
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req->evhttpreq);
	
	struct evbuffer *output_buffer = evhttp_request_get_output_buffer(req->evhttpreq);
	evbuffer_add(output_buffer, new_slots_json, sdslen(new_slots_json));
	char number[64];
	snprintf(number, sizeof(number), "%zu", sdslen(new_slots_json));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);
	
	consul_request_make(req, EVHTTP_REQ_PUT, url);
	sdsfree(new_slots_json);
	new_slots_json=NULL;
}
void remove_failed_nodes_from_global_slot_table(struct swarmkv_keyspace *ks, node_t *health_nodes, size_t n_node)
{
	size_t i=0, j=0, k=0;
	size_t slots_number_effect_by_failed_node=0;
	struct key_slot *slots_copy=NULL;
	node_t *new_owner=NULL, *old_owner=NULL;
//	char uuid_str1[37]="", uuid_str2[37]="";
	if(n_node==0)
	{
		return;
	}
	slots_copy=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		memcpy(slots_copy+i, &(ks->slot_rts[i].slot), sizeof(struct key_slot));
	}
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==node_compare(old_owner, health_nodes+j))
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

	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		old_owner=&(slots_copy[i].owner);
		for(j=0; j<n_node; j++)
		{
			if(0==node_compare(old_owner, health_nodes+j))
			{
				break;
			}
		}
		//slot owner has left the cluster.
		if(j==n_node)
		{
			if(k==slots_rebalanced_per_node && health_node_offset<n_node-1)
			{
				k=0;
				health_node_offset++;
			}
			new_owner=health_nodes+health_node_offset;
			node_copy(old_owner, new_owner);
			k++;
		}
	}
	int changed_slot_start=-1, changed_slot_end=-1;
	node_t tmp_new_node, tmp_old_node;
	if(slots_number_effect_by_failed_node)
	{
		for(i=0; i<KEYSPACE_SLOT_NUM; i++)
		{
			if(0!=node_compare(&ks->slot_rts[i].slot.owner, &slots_copy[i].owner))
			{
				if(changed_slot_start<0)
				{
					node_copy(&tmp_old_node, &ks->slot_rts[i].slot.owner);
					changed_slot_start=i;
				}
				changed_slot_end=i;
				node_copy(&tmp_new_node, &slots_copy[i].owner);
			}
			else if(changed_slot_end>0)
			{
				log_info(ks->logger, MODULE_SWARMKV_KEYSPACE,
				 		"leader %s changes slot %d-%d owner from %s to %s.",
						ks->self.addr, changed_slot_start, changed_slot_end, 
						tmp_old_node.addr, tmp_new_node.addr);
				changed_slot_start=changed_slot_end=-1;
			}
		}
		propagate_slot_table_async(ks, slots_copy, KEYSPACE_SLOT_NUM);
	}
	free(slots_copy);
	slots_copy=NULL;
	return;
}
void remove_failed_nodes_from_key_route_table(struct swarmkv_keyspace *ks, node_t *health_nodes, size_t n_node)
{
	struct key_route_entry *key_entry=NULL, *tmp_entry=NULL;
	struct slot_runtime *slot_rt=NULL;
	struct replica_node *hash_health_node=NULL, *node=NULL, *found=NULL, *tmp_node=NULL;
	size_t n_modified_key=0, n_removed_replica=0;
	return;
	for(size_t i=0; i<n_node; i++)
	{
		node=ALLOC(struct replica_node, 1);
		node_copy(&node->node, health_nodes+i);
		HASH_ADD(hh, hash_health_node, node, sizeof(node_t), node);
	}
	for(int slot_id=0; slot_id<KEYSPACE_SLOT_NUM; slot_id++)
	{
		slot_rt=ks->slot_rts+slot_id;
		if(!slot_rt->I_am_owner) continue;
		pthread_mutex_lock(&slot_rt->mutex);
		HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp_entry)
		{
			int is_modified=0;
			HASH_ITER(hh, key_entry->hash_replica, node, tmp_node)
			{
				HASH_FIND(hh, hash_health_node, &node->node, sizeof(node->node), found);
				if(!found)
				{
					HASH_DEL(key_entry->hash_replica, node);
					n_removed_replica++;
					is_modified=1;
				}
			}
			if(is_modified) n_modified_key++;
		}
		pthread_mutex_unlock(&slot_rt->mutex);
	}
	HASH_ITER(hh, hash_health_node, node, tmp_node)
	{
		HASH_DEL(hash_health_node, node);
		free(node);
	}
	if(n_modified_key>0)
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, 
				"key route table modified %zu keys and removes %zu replicas for handling failed nodes.",
				n_modified_key, n_removed_replica);
	}
}
void watch_nodes_changes_on_success(void *result, void *arg)
{
	struct evhttp_request *req= (struct evhttp_request *)result;
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	int resp_code=0;
	const char* tmp=NULL;
	node_t health_nodes[SWARMKV_NODE_MAX];
	memset(health_nodes, 0, sizeof(health_nodes));

	resp_code=evhttp_request_get_response_code(req);
	
	if(resp_code==200)
	{
		struct evbuffer *buf=NULL;
		buf=evhttp_request_get_input_buffer(req);
		size_t len = evbuffer_get_length(buf);
		size_t n_node=SWARMKV_NODE_MAX;
		sds resp_body_buff=sdsnewlen(NULL, len);
		evbuffer_copyout(buf, resp_body_buff, len);
		health_response2active_nodes(resp_body_buff, health_nodes, NULL, &n_node);
	
		sdsfree(resp_body_buff);
		struct evkeyvalq *input_headers=evhttp_request_get_input_headers(req);	
		tmp=evhttp_find_header(input_headers, "X-Consul-Index");
		sscanf(tmp, "%d", &ks->consul_nodes_modify_idx);
		remove_failed_nodes_from_key_route_table(ks, health_nodes, n_node);
		if(ks->is_leader)
		{
			remove_failed_nodes_from_global_slot_table(ks, health_nodes, n_node);
		}
	}
	consul_watch_nodes_changes_async(ks);
}
void watch_nodes_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace*)arg;
	consul_watch_nodes_changes_async(ks);
}
void consul_watch_nodes_changes_async(struct swarmkv_keyspace* ks)
{
	char url[SWARMKV_URL_MAX]="";

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1&index=%d&wait=10s", ks->db_name, ks->consul_nodes_modify_idx);
	struct future *f=future_create(__func__, watch_nodes_changes_on_success, watch_nodes_changes_on_fail, ks);
	struct consul_request *req=consul_request_new(ks->consul_client, __func__, f);
	consul_request_make(req, EVHTTP_REQ_GET, url);
}

int keyslots_init(struct swarmkv_keyspace *ks)
{
	int i=0, error=0;
	struct slot_runtime *slot_rt=NULL;
	struct key_slot *slot=NULL;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		slot=&(slot_rt->slot);
		slot->slot_id=i;
		node_copy(&slot->owner, &ks->self);
		slot_rt->expires=timeouts_open(0, &error);
		pthread_mutex_init(&slot_rt->mutex, NULL);
	}

	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?raw=1", ks->db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, ks->consul_agent_host, ks->consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	json2keyslots(resp_body, ks->slot_rts, sizeof(struct slot_runtime), offsetof(struct slot_runtime, slot), KEYSPACE_SLOT_NUM);
	sdsfree(resp_body);
	return 0;
}

static struct swarmkv_keyspace *module2keyspace(struct swarmkv_module *module)
{
	//type safety check
	assert(0==strcmp(module->name, "keyspace"));
	struct swarmkv_keyspace *ks=container_of(module, struct swarmkv_keyspace, module);
	assert(ks==module->mod_ctx);
	return ks;
}
void swarmkv_keyspace_set_exec_cmd_func(struct swarmkv_module *mod_keyspace, exec_cmd_func *func, void *func_handle)
{	
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ks->exec_cmd_func=func;
	ks->exec_cmd_handle=func_handle;
	return;
}
void swarmkv_keyspace_set_monitor_handle(struct swarmkv_module *mod_keyspace, struct swarmkv_module *mod_monitor)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ks->mod_monitor=mod_monitor;
	return;
}
void keyspace_lock(struct swarmkv_module *mod_keyspace, enum cmd_key_flag flag, const sds key)
{
	if(!key) return;
	int slot_id=key_hash_slot(key, sdslen(key));
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	struct slot_runtime *slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	return;
}
void keyspace_unlock(struct swarmkv_module *mod_keyspace, enum cmd_key_flag flag, const sds key)
{
	if(!key) return;
	int slot_id=key_hash_slot(key, sdslen(key));
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	struct slot_runtime *slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_unlock(&slot_rt->mutex);
	return;
}

struct swarmkv_module *swarmkv_keyspace_new(struct swarmkv_options *opts, const char *db_name, struct log_handle *logger, char **err)
{
	struct swarmkv_keyspace *ks=ALLOC(struct swarmkv_keyspace, 1);
	strncpy(ks->module.name, "keyspace", sizeof(ks->module.name));
	ks->module.mod_ctx=ks;
	ks->module.lock=keyspace_lock;
	ks->module.unlock=keyspace_unlock;
	strncpy(ks->db_name, db_name, sizeof(ks->db_name));
	node_init(&ks->self, opts->cluster_announce_ip, opts->cluster_announce_port);
	uuid_copy(ks->uuid, opts->bin_uuid);
	ks->opts=opts;
	ks->consul_agent_port=opts->consul_port;
    strncpy(ks->consul_agent_host, opts->consul_agent_host, sizeof(ks->consul_agent_host));

    ks->evbase = event_base_new();
	ks->logger = logger;
	ks->dryrun= opts->dryrun;

	ks->consul_client=consul_client_new(ks->opts->consul_agent_host, ks->opts->consul_port, ks->evbase, ks->logger);

	int ret=-1;
	ret=keyslots_init(ks);
	if(ret<0)
	{
		asprintf(err, "key slots init failed.");
        log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots init failed.");
		goto error_out;
	}

	consul_watch_slots_changes_async(ks);
	consul_watch_nodes_changes_async(ks);

	pthread_create(&(ks->thr), NULL, swarmkv_keyspace_thread, (void *) ks);

	if(ks->dryrun)
	{
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "work on dry run mode.");
		return &(ks->module);
	}
	ks->http_server=http_server_new(ks->evbase, &(opts->health_check_port), ks);
	if(!ks->http_server)
	{
		asprintf(err, "health check HTTP server start failed.");
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "health check HTTP server start failed.");
		goto error_out;
	}
	if(opts->health_check_announce_port==0)
	{
		opts->health_check_announce_port=opts->health_check_port;
	}
	ret=consul_service_register(ks);
	if(ret<0)
	{	
		asprintf(err, "consul service register failed.");
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "consul service register failed.");
		goto error_out;
	}
	if(opts->run_for_leader_flag)
	{
		consul_watch_leadership_changes_async(ks);
	}
	return &(ks->module);

error_out:
	free(ks);
	return NULL;
}

void swarmkv_keyspace_free(struct swarmkv_module* module)
{
	struct swarmkv_keyspace *ks = module2keyspace(module);

	event_base_loopexit(ks->evbase, NULL);
	pthread_join(ks->thr, NULL);
	if(ks->http_server)
	{
		evhttp_free(ks->http_server);
		ks->http_server=NULL;
	}
	struct key_route_entry *key_entry=NULL, *tmp_key_entry=NULL;
	int i=0;
	struct slot_runtime *slot_rt=NULL;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		timeouts_close(slot_rt->expires);
		slot_rt->expires=NULL;
		
		HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp_key_entry)
		{
			HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
			key_entry_free(key_entry);
		}
		pthread_mutex_destroy(&slot_rt->mutex);
	}
	consul_client_free(ks->consul_client);
	ks->consul_client=NULL;
	event_base_free(ks->evbase);
	free(ks);
	return;
}
void swarmkv_keyspace_info(struct swarmkv_module *mod_keyspace, struct keyspace_info *info)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int i=0;
	struct slot_runtime *slot_rt=NULL;

	info->health_check_port=ks->opts->health_check_announce_port;
	info->keys=0;
	info->expires=0;
	info->slots=0;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		if(!slot_rt->I_am_owner) continue;
		info->slots++;
		pthread_mutex_lock(&slot_rt->mutex);
		info->keys+=HASH_COUNT(slot_rt->keyroute_table);
		info->expires+=timeouts_count(slot_rt->expires);
		pthread_mutex_unlock(&slot_rt->mutex);
	}
}
enum cmd_exec_result keyslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
	/* KEYSLOT key*/
	char *key=cmd->argv[1];
	int slot_id=key_hash_slot(key, strlen(key));
	*reply=swarmkv_reply_new_integer(slot_id);
	return FINISHED;
}
enum cmd_exec_result keyspace_setslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE SETSLOT <slot> IMPORTING|MIGRATING|NODE|STABLE IP:port */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	node_t peer_node;

	enum slot_state state;
	sds str_state=cmd->argv[3];
	sds str_addr=cmd->argv[4];

	int ret_string2node=0;
	if(0==strcasecmp(str_state, "IMPORTING"))
	{
		state=STATE_IMPORTING;
	}
	else if(0==strcasecmp(str_state, "MIGRATING"))
	{
		state=STATE_MIGRATING;
	}
	else if(0==strcasecmp(str_state, "NODE"))
	{
		state=STATE_STABLE;
	}
	else if(0==strcasecmp(str_state, "STABLE"))
	{
		state=STATE_STABLE;

	}
	else
	{
		*reply=swarmkv_reply_new_error(error_arg_string_should_be, str_state, "IMPORTING|MIGRATING|NODE|STABLE");
		return FINISHED;
	}

	if(state!=STATE_STABLE)
	{
		if(cmd->argc<5)
		{
			*reply=swarmkv_reply_new_error(error_need_additional_arg, str_state);
			return FINISHED;
		}
		ret_string2node=node_init_from_sds(&peer_node, str_addr);
		if(ret_string2node!=0)
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_address, str_state);
			return FINISHED;
		}
	}

	struct slot_runtime *slot_rt=NULL;	
	long long slot_id=-1;
	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	slot_rt=&(ks->slot_rts[slot_id]);
	
	pthread_mutex_lock(&slot_rt->mutex);
	slot_rt->state=state;
	if(state!=STATE_STABLE)
	{
		node_copy(&slot_rt->rebalancing_peer, &peer_node);
	}
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_status("OK");

	return FINISHED;

}
static void key_entry_serialize(struct key_route_entry *key_entry, char **blob, size_t *blob_sz)
{
	char *mpack_buff=NULL;
	size_t mpack_sz=0;	
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);
	mpack_build_map(&writer);
	
	mpack_write_cstr(&writer, "timeout");
	mpack_write_u64(&writer, key_entry->abs_timeout);

	mpack_write_cstr(&writer, "replicas");

	mpack_build_array(&writer);	
	struct replica_node *replica=NULL, *tmp=NULL;
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		mpack_write_cstr(&writer, replica->node.addr);
	}	
	mpack_complete_array(&writer);
	
	mpack_complete_map(&writer);
	mpack_writer_destroy(&writer);
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
	return;
}
static void key_entry_merge(struct key_route_entry *key_entry, const char *blob, size_t blob_sz)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t mpnode_replicas, mpnode_a_replica;
	node_t replica;
	key_entry->abs_timeout=mpack_node_u64(mpack_node_map_cstr(root, "timeout"));
	mpnode_replicas=mpack_node_map_cstr(root, "replicas");
	
	size_t n_replica=mpack_node_array_length(mpnode_replicas);
	for(size_t i=0; i<n_replica; i++)
	{
		mpnode_a_replica=mpack_node_array_at(mpnode_replicas, i);
        memset(&replica, 0, sizeof(node_t));
		mpack_node_copy_cstr(mpnode_a_replica, replica.addr, sizeof(replica.addr));
		key_entry_add_replica_node(key_entry, &replica);
	}
	if(mpack_tree_destroy(&tree)!= mpack_ok)
	{
		assert(0);
	}
	return;
}
static void key_slot_serialize(const struct slot_runtime *slot_rt, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct key_route_entry *key_entry=NULL, *tmp_entry=NULL;
	char *keyentry_blob=NULL;
	size_t keyentry_blob_sz=0;
	

	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);
	mpack_build_map(&writer);

	mpack_write_cstr(&writer, "slot_id");
	mpack_write_int(&writer, slot_rt->slot.slot_id);


	mpack_write_cstr(&writer, "key_route_table");	
	mpack_build_array(&writer);
	HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp_entry)
	{	
		mpack_write_str(&writer, key_entry->key, sdslen(key_entry->key));
		key_entry_serialize(key_entry, &keyentry_blob, &keyentry_blob_sz);
		mpack_write_str(&writer, keyentry_blob, keyentry_blob_sz);
		free(keyentry_blob);
		keyentry_blob_sz=0;
	}
	mpack_complete_array(&writer);
	mpack_complete_map(&writer);
	mpack_writer_destroy(&writer);
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
	return;
}
long long key_slot_merge(struct slot_runtime *slot_rt, const char *blob, size_t blob_sz)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t mpnode_key, mpnode_keyentry, mpnode_table;
	int __attribute__((__unused__))slot_id=mpack_node_int(mpack_node_map_cstr(root, "slot_id"));
	assert(slot_id==slot_rt->slot.slot_id);
	mpnode_table=mpack_node_map_cstr(root, "key_route_table");

	long long new_added_entry_num=0;
	
	size_t entry_num=mpack_node_array_length(mpnode_table);
	struct key_route_entry *key_entry=NULL;
	
	for(size_t i=0; i<entry_num; i+=2)
	{
		mpnode_key=mpack_node_array_at(mpnode_table, i);
		mpnode_keyentry=mpack_node_array_at(mpnode_table, i+1);
		HASH_FIND(hh, slot_rt->keyroute_table, mpack_node_str(mpnode_key), mpack_node_strlen(mpnode_key), key_entry);
		if(!key_entry)
		{
			key_entry=key_entry_new_len(mpack_node_str(mpnode_key), mpack_node_strlen(mpnode_key));
			HASH_ADD_KEYPTR(hh, slot_rt->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			new_added_entry_num++;
		}
		key_entry_merge(key_entry, mpack_node_str(mpnode_keyentry), mpack_node_strlen(mpnode_keyentry));
		if(key_entry->is_expiring)
		{
			timeouts_del(slot_rt->expires, &key_entry->timeout_handle);
		}
		if(key_entry->abs_timeout)
		{
			timeout_init(&key_entry->timeout_handle, TIMEOUT_ABS);
			key_entry->is_expiring=1;				
			timeouts_add(slot_rt->expires, &key_entry->timeout_handle, key_entry->abs_timeout); 			
		}
	}
	if(mpack_tree_destroy(&tree)!= mpack_ok)
	{
		assert(0);
	}
	return new_added_entry_num;
}

enum cmd_exec_result keyspace_getkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd,  const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE GETKEYSINSLOT slot */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	struct slot_runtime* slot_rt=NULL;
	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	slot_rt=ks->slot_rts+slot_id;
	char *blob=NULL;
	size_t blob_sz;
	pthread_mutex_lock(&slot_rt->mutex);
	key_slot_serialize(slot_rt, &blob, &blob_sz);
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_string(blob, blob_sz);
	free(blob);
	blob=NULL;
	return FINISHED;
}

enum cmd_exec_result keyspace_addkeystoslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE ADDKEYSTOSLOT slot blob */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);


	long long slot_id=-1;
	long long new_added_entry_num=0;
	struct slot_runtime* slot_rt=NULL;
	const sds blob=cmd->argv[3];

	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}

	slot_rt=ks->slot_rts+slot_id;

	pthread_mutex_lock(&slot_rt->mutex);
	new_added_entry_num=key_slot_merge(slot_rt, blob, sdslen(blob));
	pthread_mutex_unlock(&slot_rt->mutex);

	*reply=swarmkv_reply_new_integer(new_added_entry_num);
	return FINISHED;
}

enum cmd_exec_result keyspace_delslotkeys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE DELSLOTKEYS slot */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	struct slot_runtime* slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	long long delete_num=0;

	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}

	slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp)
	{
		HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
		if(key_entry->is_expiring)
		{
			timeouts_del(slot_rt->expires, &key_entry->timeout_handle);
		}
		key_entry_free(key_entry);
		delete_num++;
	}
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_integer(delete_num);
	return FINISHED;
}
enum cmd_exec_result keyspace_countkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE COUNTKEYSINSLOT slot*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	struct slot_runtime* slot_rt=NULL;
	long long key_num=0;

	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	slot_rt=ks->slot_rts+slot_id;
	pthread_mutex_lock(&slot_rt->mutex);
	key_num=HASH_COUNT(slot_rt->keyroute_table);
	pthread_mutex_unlock(&slot_rt->mutex);
	*reply=swarmkv_reply_new_integer(key_num);
	return FINISHED;
}
enum KEYSPACE_OP
{
	KEYSPACE_RLIST,
	KEYSPACE_RADD,
	KEYSPACE_XRADD,
	KEYSPACE_DEL,
    KEYSPACE_RDEL
};

enum cmd_exec_result key_route_generic(struct swarmkv_keyspace *ks, enum KEYSPACE_OP op, const sds key, const node_t *new_replica, struct swarmkv_reply **reply)
{
	struct key_route_entry *key_entry=NULL;
	int slot_id=-1;
	struct slot_runtime *slot_rt=NULL;

	slot_id=key_hash_slot(key, sdslen(key));
	slot_rt=ks->slot_rts+slot_id;
	if(!slot_rt->I_am_owner && slot_rt->state!=STATE_IMPORTING)
	{
		*reply=swarmkv_reply_new_node(&(slot_rt->slot.owner), 1);
		return REDIRECT;
	}
	
	HASH_FIND(hh, slot_rt->keyroute_table, key, sdslen(key), key_entry);
	if(!key_entry && slot_rt->state==STATE_MIGRATING)
	{
		*reply=swarmkv_reply_new_node(&(slot_rt->rebalancing_peer), 1);
		return REDIRECT;
	}

	switch(op)
	{
		case KEYSPACE_RLIST:
			if(key_entry)
			{
				*reply=key_entry_list_replica_nodes(key_entry);
			}
			else
			{
				*reply=swarmkv_reply_new_nil();
			}
			break;
		case KEYSPACE_RADD:
			if(!key_entry)
			{
				key_entry=key_entry_new(key);
				HASH_ADD_KEYPTR(hh, slot_rt->keyroute_table, key_entry->key, sdslen(key_entry->key), key_entry);
			}
			key_entry_add_replica_node(key_entry, new_replica);
			*reply=key_entry_list_replica_nodes(key_entry);
			break;
		case KEYSPACE_XRADD:
			if(key_entry)
			{
				key_entry_add_replica_node(key_entry, new_replica);
				*reply=key_entry_list_replica_nodes(key_entry);
			}
			else
			{
				*reply=swarmkv_reply_new_nil();
			}
			break;
		case KEYSPACE_DEL:
			if(key_entry)
			{
				HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
				key_entry_deletion_notification(key_entry, ks->exec_cmd_func, ks->exec_cmd_handle);
				key_entry_free(key_entry);
				*reply=swarmkv_reply_new_integer(1);
			}
			else
			{
				*reply=swarmkv_reply_new_integer(0);
			}
			break;
        case KEYSPACE_RDEL:
            if(key_entry)
            {
                size_t n_existed_replica=0;
                key_entry_rdel_replica_node(key_entry, new_replica);
                n_existed_replica=HASH_COUNT(key_entry->hash_replica);
                if(n_existed_replica==0)
                {
                    HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
                }
                *reply=swarmkv_reply_new_integer(1);
            }
            else
            {
                 *reply=swarmkv_reply_new_integer(0);
            }
            break;
		default:
			assert(0);
			break;
	}
	return FINISHED;
}

UT_icd ut_swarmkv_reply_pointer_icd = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};
enum cmd_exec_result keyspace_keys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* KEYSPACE KEYS key-pattern */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	size_t i=0, n_matched=0;
	struct slot_runtime *slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	int is_matched=0;
	UT_array* matched_replies=NULL;
	struct swarmkv_reply *r=NULL;

	const sds pattern=cmd->argv[2];
	utarray_new(matched_replies, &ut_swarmkv_reply_pointer_icd);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		pthread_mutex_lock(&slot_rt->mutex);
		if(!slot_rt->I_am_owner && slot_rt->state!=STATE_IMPORTING)
		{
			pthread_mutex_unlock(&slot_rt->mutex);
			continue;
		}
		
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
	return FINISHED;
}
enum cmd_exec_result keyspace_rlist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE RLIST <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];
	
	ret=key_route_generic(ks, KEYSPACE_RLIST, key, NULL, reply);
	return ret;
}

enum cmd_exec_result keyspace_radd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE RADD <key> IP:port*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];

	node_t new_replica;
	if(cmd->argc==4)
	{
		if(0>node_init_from_sds(&new_replica, cmd->argv[3]))
		{
			*reply=swarmkv_reply_new_error(error_arg_not_valid_address, cmd->argv+3);
			return ret;
		}
	}
	else
	{
		node_copy(&new_replica, &ks->self);
	}
	ret=key_route_generic(ks, KEYSPACE_RADD, key, &new_replica, reply);
	return ret;
}
enum cmd_exec_result keyspace_xradd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE XRADD <key> IP:port*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];
	
	node_t new_replica;
	if(0>node_init_from_sds(&new_replica, cmd->argv[3]))
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_address, cmd->argv+3);
		return ret;
	}
	ret=key_route_generic(ks, KEYSPACE_XRADD, key, &new_replica, reply);
	return ret;
}

enum cmd_exec_result del_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*DEL <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[1];
	ret=key_route_generic(ks, KEYSPACE_DEL, key, NULL, reply);
	return ret;
}

enum cmd_exec_result keyspace_rdel_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/*KEYSPACE RDEL <key IP:port> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];

    node_t del_replica;
	if(0>node_init_from_sds(&del_replica, cmd->argv[3]))
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_address, cmd->argv+3);
		return ret;
	}
	ret=key_route_generic(ks, KEYSPACE_RDEL, key, &del_replica, reply);
	return ret;
}

enum KEY_TTL_OP
{
	KEY_TTL_SET,
	KEY_TTL_GET,
	KEY_TTL_REMOVE
};
enum cmd_exec_result key_ttl_generic(struct swarmkv_keyspace *ks, enum KEY_TTL_OP op, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	const sds key=cmd->argv[1];
	long seconds=0;
	char *endptr=NULL;

	enum cmd_exec_result ret=FINISHED;

	int slot_id=key_hash_slot(key, sdslen(key));
	struct slot_runtime *slot_rt=ks->slot_rts+slot_id;
	struct key_route_entry *key_entry=NULL;
	if(!slot_rt->I_am_owner && slot_rt->state != STATE_IMPORTING)
	{
		*reply=swarmkv_reply_new_node(&(slot_rt->slot.owner), 1);
		return REDIRECT;
	}
	
	HASH_FIND(hh, slot_rt->keyroute_table, key, sdslen(key), key_entry);
	if(!key_entry)
	{
		if(slot_rt->state==STATE_MIGRATING)
		{
			*reply=swarmkv_reply_new_node(&(slot_rt->rebalancing_peer), 1);
			ret=REDIRECT;
		}
		else
		{
			*reply=swarmkv_reply_new_integer((op==KEY_TTL_SET||op==KEY_TTL_REMOVE)?0:-2);
			ret=FINISHED;
		}
		return ret;
	}
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	switch(op)
	{
		case KEY_TTL_SET:

			seconds=strtol(cmd->argv[2], &endptr, 10);
			if(*endptr!='\0' || seconds>INT_MAX)
			{
				*reply=swarmkv_reply_new_error(error_value_not_integer);
			}
			else
			{
				if(key_entry->is_expiring)
				{
					timeouts_del(slot_rt->expires, &key_entry->timeout_handle);
				}
				timeout_init(&key_entry->timeout_handle, TIMEOUT_ABS);
				key_entry->is_expiring=1;
				key_entry->abs_timeout=now.tv_sec+seconds;
				timeouts_add(slot_rt->expires, &key_entry->timeout_handle, key_entry->abs_timeout);				
				*reply=swarmkv_reply_new_integer(1);
			}
			
			break;
		case KEY_TTL_GET:
			if(key_entry->is_expiring)
			{
				int ttl=key_entry->abs_timeout - now.tv_sec;
				*reply=swarmkv_reply_new_integer(ttl);
			}
			else
			{
				*reply=swarmkv_reply_new_integer(-1);
			}
			break;
		case KEY_TTL_REMOVE:
			if(key_entry->is_expiring)
			{
				timeouts_del(slot_rt->expires, &(key_entry->timeout_handle));
				key_entry->is_expiring=0;
				*reply=swarmkv_reply_new_integer(1);
			}
			else
			{
				*reply=swarmkv_reply_new_integer(0);
			}
			break;
		default:
			assert(0);
			break;
	}
	return FINISHED;
}
enum cmd_exec_result expire_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* EXPIRE key seconds */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_SET, cmd, reply);
	return ret;
}
enum cmd_exec_result ttl_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* TTL key */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_GET, cmd, reply);
	return ret;
}
enum cmd_exec_result persist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
/* PERSIST key */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_REMOVE, cmd, reply);
	return ret;

}

#define ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP 1000
void keyspace_active_expire_cycle(struct swarmkv_keyspace *ks)
{	
	struct timespec now;
	int i=0, j=0;
	struct slot_runtime *slot_rt=NULL;
	struct timeout *t=NULL;	
	struct key_route_entry *key_entry=NULL;
	struct timespec start, end;
	int has_key_expired=0;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &start);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		if(!slot_rt->I_am_owner) continue;
		pthread_mutex_lock(&slot_rt->mutex);

		clock_gettime(CLOCK_REALTIME, &now);
		timeouts_update(slot_rt->expires, now.tv_sec);
		t=timeouts_get(slot_rt->expires);
		while(t && j<ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP)
		{
			key_entry=container_of(t, struct key_route_entry, timeout_handle);
			assert(key_entry->is_expiring);
			HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
			key_entry_deletion_notification(key_entry, ks->exec_cmd_func, ks->exec_cmd_handle);
			key_entry_free(key_entry);
			t=timeouts_get(slot_rt->expires);
			j++;
			has_key_expired=1;
		}
		pthread_mutex_unlock(&slot_rt->mutex);
	}
	clock_gettime(CLOCK_MONOTONIC_COARSE, &end);
	if(has_key_expired)
	{
		swarmkv_monitor_record_event(ks->mod_monitor, "keyspace-expire-cycle", timespec_diff_usec(&start, &end));
	}
}


