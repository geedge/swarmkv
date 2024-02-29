#include "swarmkv_keyspace.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include "swarmkv_monitor.h"
#include "swarmkv_error.h"
#include "http_client.h"

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
static int slot_is_my_thread(int slot_id, int thread_id, int nr_threads)
{
	return (slot_id%nr_threads)==thread_id;
}
struct crdt_op_ctx
{
	node_t peer;
	int is_del;
	sds key;
};
void crdt_op_ctx_free(struct crdt_op_ctx *ctx)
{
	sdsfree(ctx->key);
	free(ctx);
}

void crdt_op_on_reply(const struct swarmkv_reply *reply, void * arg)
{
	struct crdt_op_ctx *ctx = (struct crdt_op_ctx*)arg;
	if(ctx->is_del)
	{
		if(reply->type==SWARMKV_REPLY_INTEGER && reply->integer !=1)
		{
	//		printf("crdt del %s at %s:%u failed.\n", ctx->key, ctx->peer.ip_addr, ctx->peer.cluster_port);
		}
	}
	crdt_op_ctx_free(ctx);
	return;
}
void key_entry_deletion_notification(struct key_route_entry *key_entry, struct swarmkv *exec_cmd_handle)
{
	struct replica_node *replica=NULL, *tmp=NULL;
	struct swarmkv_cmd *crdt_del_cmd=swarmkv_cmd_new(3);
	crdt_del_cmd->argv[0]=sdsnew("crdt");
	crdt_del_cmd->argv[1]=sdsnew("del");
	crdt_del_cmd->argv[2]=sdsdup(key_entry->key);
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		struct crdt_op_ctx *ctx=ALLOC(struct crdt_op_ctx, 1);
		node_copy(&ctx->peer, &replica->node);
		ctx->key=sdsdup(key_entry->key);
		ctx->is_del=1;
		swarmkv_async_command_on_argv(exec_cmd_handle, crdt_op_on_reply, ctx, replica->node.addr, crdt_del_cmd->argc, crdt_del_cmd->argv);
	}
	swarmkv_cmd_free(crdt_del_cmd);
	return;
}

void key_entry_meet_replica(struct key_route_entry *key_entry, struct swarmkv *exec_cmd_handle)
{
	struct replica_node *replica=NULL, *tmp=NULL;
	int n_replica=HASH_COUNT(key_entry->hash_replica), i=0;
	struct swarmkv_cmd *crdt_meet_cmd=swarmkv_cmd_new(3+n_replica);
	crdt_meet_cmd->argv[0]=sdsnew("crdt");
	crdt_meet_cmd->argv[1]=sdsnew("meet");
	crdt_meet_cmd->argv[2]=sdsdup(key_entry->key);
	
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		crdt_meet_cmd->argv[3+i]=node_addr2sds(&replica->node);
		i++;
	}
	assert(i==n_replica);
	HASH_ITER(hh, key_entry->hash_replica, replica, tmp)
	{
		struct crdt_op_ctx *ctx=ALLOC(struct crdt_op_ctx, 1);
		node_copy(&ctx->peer, &replica->node);
		ctx->key=sdsdup(key_entry->key);
		swarmkv_async_command_on_argv(exec_cmd_handle, crdt_op_on_reply, ctx, replica->node.addr, crdt_meet_cmd->argc, crdt_meet_cmd->argv);
	}
	swarmkv_cmd_free(crdt_meet_cmd);
	return;
}

struct slot_runtime
{
	struct key_slot slot;
	enum slot_state state;
	node_t rebalancing_peer;//rebalancing_peer is used for state MIGRATING and IMPORTING

	struct key_route_entry *keyroute_table;
};
void http_connection_close_callback(struct evhttp_connection *conn, void *arg)
{
//	struct consul_task *task=(struct consul_task *)arg;
	//printf("http connection %s %p closed\n", task->task_name, conn);
	return;
}
struct ks_thread
{
	int thread_id;
	struct timeouts *expires;
	struct http_client *consul_client;
	int consul_slots_modify_idx;
	int consul_nodes_modify_idx;
	struct swarmkv_keyspace *ref_ks;
	struct event_base *evbase;	//reference of swarmkv

};
struct swarmkv_keyspace
{
	struct swarmkv_module module;
	struct swarmkv_options *opts;
	char db_name[SWARMKV_SYMBOL_MAX];
	pthread_t thr;
	int readable_tid;
	node_t self;
	uuid_t uuid;


	struct ks_thread *threads;

	struct slot_runtime slot_rts[KEYSPACE_SLOT_NUM];

	char consul_agent_host[MAX_IPV4_ADDR_LEN];
	unsigned int consul_agent_port;

	int init_success;

	int is_leader;
	int consul_lead_key_modify_idx;

	char consul_service_id[SWARMKV_SYMBOL_MAX];
	char consul_check_id[SWARMKV_SYMBOL_MAX];
	char consul_session_id[SWARMKV_SYMBOL_MAX];

	struct evhttp *http_server;


	//reference, no need free	
	struct swarmkv *exec_cmd_handle;
	struct swarmkv_module *mod_monitor;
	struct log_handle *logger;

};


struct http_client;

void consul_acquire_session_lock_async(struct ks_thread* thr);
void consul_watch_leadership_changes_async(struct ks_thread* thr);

void consul_watch_nodes_changes_async(struct ks_thread* ks);
void consul_watch_slots_changes_async(struct ks_thread* ks);


void health_check_cb(struct evhttp_request *request, void *arg)
{
	struct swarmkv_keyspace *ks=(struct swarmkv_keyspace *)arg;
	struct evbuffer* evbuf_reply = evbuffer_new();
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(request);

	 //HTTP header
	evhttp_add_header(output_headers, "Server", SWARMKV_VERSION);
	evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=UTF-8");//utf8
	evhttp_add_header(output_headers, "Connection", "close");
	
	evbuffer_add_printf(evbuf_reply, "SwarmKV node of cluster %s is running", ks->db_name);
	evhttp_send_reply(request, HTTP_OK, "OK", evbuf_reply);
	evbuffer_free(evbuf_reply);
	struct evhttp_connection *conn=evhttp_request_get_connection(request);
	struct bufferevent * bev=evhttp_connection_get_bufferevent(conn);
	bufferevent_priority_set(bev, 0);
	return;
}

struct evhttp *http_server_new(struct event_base *evbase, unsigned int *port_listen, void *arg)
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

void consul_session_create_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
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
		strncpy(thr->ref_ks->consul_session_id, session_id->valuestring, sizeof(thr->ref_ks->consul_session_id));
		cJSON_Delete(session_create_response);
		consul_acquire_session_lock_async(thr);

		is_running_for_leader=1;
		log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "session %s is created.", thr->ref_ks->consul_session_id);
	}
	else
	{
		log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "session creation failed: HTTP code %d, %s.",
			resp_code,
			out);
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(thr);
	}
	sdsfree(out);
	return;
}
void consul_session_create_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session create failed: %s.", what);
	return;
}
void consul_create_session_async(struct ks_thread *thr)
{
	cJSON *session=NULL, *service_check_array=NULL, *service_check=NULL;
	session=cJSON_CreateObject();
	cJSON_AddStringToObject(session, "LockDelay", "5s");
	cJSON_AddStringToObject(session, "Name", "swarmkv-leader-election-lock");
	cJSON_AddStringToObject(session, "Behavior", "release");

	service_check=cJSON_CreateObject();
	cJSON_AddStringToObject(service_check, "ID", thr->ref_ks->consul_check_id);
	service_check_array=cJSON_CreateArray();
	cJSON_AddItemToArray(service_check_array, service_check);
	cJSON_AddItemToObject(session, "ServiceChecks", service_check_array);

	char *payload=NULL;
	payload=cJSON_Print(session);
	cJSON_Delete(session);

	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/create");

	struct future *f=future_create(__func__, consul_session_create_on_success, consul_session_create_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);

	struct evkeyvalq *output_headers = http_request_get_output_headers(req);
	
	struct evbuffer *output_buffer = http_request_get_output_buffer(req);
	evbuffer_add(output_buffer, payload, strlen(payload));
	char number[64];
	snprintf(number, sizeof(number), "%zu", strlen(payload));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	http_request_make(req, EVHTTP_REQ_PUT, url);
	free(payload);
	return;

}
void consul_session_check_on_success(void *result, void *arg)
{
	int resp_code=0;
	cJSON *session_check=NULL;
	int session_check_array_size=0;
	struct ks_thread *thr=(struct ks_thread*)arg;
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
			consul_create_session_async(thr);
			log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE,
				"session %s is not existed, start to create a new session.", thr->ref_ks->consul_session_id);
			memset(thr->ref_ks->consul_session_id, 0, sizeof(thr->ref_ks->consul_session_id));
		}
		else
		{
			consul_acquire_session_lock_async(thr);
			log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "session %s is valid, run for leader.", thr->ref_ks->consul_session_id);
		}
		is_running_for_leader=1;
	}
	else
	{
		log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "session check failed: HTTP code %d.", resp_code);
	}
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(thr);
	}
	return;
}
void consul_session_check_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session check failed: %s.", what);
	return;
}
void consul_session_check_async(struct ks_thread *thr)
{
	assert(thr->thread_id==0);
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/session/info/%s", thr->ref_ks->consul_session_id);
	
	struct future *f=future_create(__func__, consul_session_check_on_success, consul_session_check_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);
	http_request_make(req, EVHTTP_REQ_GET, url);
	return;

}
void acquire_session_lock_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
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
			thr->ref_ks->is_leader=1;
			log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "become cluster leader.");
		}
		else
		{
			log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "lose the election.");
			thr->ref_ks->is_leader=0;
		}
		sdsfree(resp_body_buff);
	}
	else
	{
		log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "run for leader failed: HTTP Code %d.", resp_code);
	}
	consul_watch_leadership_changes_async(thr);
}
void acquire_session_lock_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "consul session lock aquire failed: %s.", what);
	consul_watch_leadership_changes_async(thr);
	return;
}
void consul_acquire_session_lock_async(struct ks_thread *thr)
{
	assert(thr->thread_id==0);
	sds req_body=node_print_json(&thr->ref_ks->self, thr->ref_ks->uuid);

	char url[SWARMKV_URL_MAX];
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?acquire=%s", thr->ref_ks->db_name, thr->ref_ks->consul_session_id);

	struct future *f=future_create(__func__, acquire_session_lock_on_success, acquire_session_lock_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);

	struct evbuffer* output_buffer = http_request_get_output_buffer(req);
	evbuffer_add(output_buffer, req_body, sdslen(req_body));


	char number[64];
	struct evkeyvalq *output_headers = http_request_get_output_headers(req);
	snprintf(number, sizeof(number), "%zu", sdslen(req_body));

	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	http_request_make(req, EVHTTP_REQ_PUT, url);
	sdsfree(req_body);

}
void consul_run_for_leader_async(struct ks_thread *thr)
{
	if(0==strlen(thr->ref_ks->consul_session_id))
	{
		consul_create_session_async(thr);
	}
	else
	{
		consul_session_check_async(thr);
	}
	return;
}
void watch_leadership_changes_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request*)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
	assert(thr->thread_id==0);//only thread 0 can run this function


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
		thr->ref_ks->consul_lead_key_modify_idx=modify_idx->valueint;
		session=cJSON_GetObjectItem(metadata, "Session");
		if(!session || 0==strlen(session->valuestring))
		{
			log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "cluster losts leader, start to run for leader.");
			consul_run_for_leader_async(thr);
			is_running_for_leader=1;
		}
		if(!session || 0!=strcmp(session->valuestring, thr->ref_ks->consul_session_id))
		{
			if(thr->ref_ks->is_leader)
			{
				log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "node is no longer a leader.");
				thr->ref_ks->is_leader=0;
			}
		}
		cJSON_Delete(metadata_array);
		sdsfree(resp_body_buff);
	}
	else if(resp_code==404)
	{
		log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "no leader key, start to run for leader.");
		consul_run_for_leader_async(thr);
		is_running_for_leader=1;
	}
	else
	{
		log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: HTTP code %d.", resp_code);
	}
	
	if(!is_running_for_leader)
	{
		consul_watch_leadership_changes_async(thr);
	}
}
void watch_leadership_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "watch leadership changes failed: %s.", what);
	return;
}
void consul_watch_leadership_changes_async(struct ks_thread *thr)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/lead?index=%d&wait=10s", thr->ref_ks->db_name, thr->ref_ks->consul_lead_key_modify_idx);

	struct future *f=future_create(__func__, watch_leadership_changes_on_success, watch_leadership_changes_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);
	http_request_make(req, EVHTTP_REQ_GET, url);
	return;
}
void watch_slots_changes_on_success(void *result, void *arg)
{
    struct evhttp_request *req=(struct evhttp_request*)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
	
	cJSON *metadata_array=NULL, *metadata=NULL, *value=NULL, *modify_idx=NULL;
	int	i=0,resp_code=0;

    resp_code=evhttp_request_get_response_code(req);
    if(resp_code!=200)
    {
        log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "watch slot changes failed: HTTP code %d.", resp_code);
        consul_watch_slots_changes_async(thr);
	    return;
    }
    struct key_slot *new_slots=NULL;
    struct evbuffer *buf=NULL;
    buf=evhttp_request_get_input_buffer(req);
    size_t len = evbuffer_get_length(buf);
    sds resp_body_buff=sdsnewlen(SDS_NOINIT, len);
    evbuffer_copyout(buf, resp_body_buff, len);

	metadata_array=cJSON_Parse(resp_body_buff);
	metadata=cJSON_GetArrayItem(metadata_array, 0);
	modify_idx=cJSON_GetObjectItem(metadata, "ModifyIndex");
	thr->consul_slots_modify_idx=modify_idx->valueint;
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
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "thread %d key slots update started.", thr->thread_id);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		if(!slot_is_my_thread(i, thr->thread_id, thr->ref_ks->opts->nr_worker_threads)) continue;

		slot_rt=thr->ref_ks->slot_rts+i;
		slot=&slot_rt->slot;
		owner=&(slot->owner);
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
	}
	log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "thread %d key slots update finished.", thr->thread_id);
	cJSON_Delete(metadata_array);
	free(new_slots);
	consul_watch_slots_changes_async(thr);
	return;
}
void watch_slots_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	consul_watch_slots_changes_async(thr);
}
void consul_watch_slots_changes_async(struct ks_thread *thr)
{
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?index=%d", thr->ref_ks->db_name, thr->consul_slots_modify_idx);
	struct future *f=future_create(__func__, watch_slots_changes_on_success, watch_slots_changes_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);
	http_request_make(req, EVHTTP_REQ_GET, url);
	return;
}
void propagate_slot_table_on_success(void *result, void *arg)
{
	struct evhttp_request *req=(struct evhttp_request *)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
	int resp_code=0;

	resp_code=evhttp_request_get_response_code(req);	
	if(resp_code==200)
	{
		log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate success.");
	}
	else
	{
		log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed.");
	}
	return;
}
void propagate_slot_table_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots table propagate failed: %s",
			what);
}
void propagate_slot_table_async(struct ks_thread *thr, struct key_slot new_slot[], size_t slot_num)
{
	sds new_slots_json=NULL;
	new_slots_json=keyslots2json(new_slot, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", thr->ref_ks->db_name);
	struct future *f=future_create(__func__, propagate_slot_table_on_success, propagate_slot_table_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);

//	evhttp_connection_set_timeout(req->evhttpconn, 20);
	
	struct evkeyvalq *output_headers = http_request_get_output_headers(req);
	char number[64];
	snprintf(number, sizeof(number), "%zu", sdslen(new_slots_json));
	evhttp_add_header(output_headers, "Content-Type", "application/json");
	evhttp_add_header(output_headers, "Content-Length", number);

	struct evbuffer *output_buffer = http_request_get_output_buffer(req);
	evbuffer_add(output_buffer, new_slots_json, sdslen(new_slots_json));
	
	http_request_make(req, EVHTTP_REQ_PUT, url);
	sdsfree(new_slots_json);
	new_slots_json=NULL;
}
void remove_failed_nodes_from_global_slot_table(struct ks_thread *thr, node_t *health_nodes, size_t n_node)
{
	size_t i=0, j=0, k=0;
	size_t slots_number_effect_by_failed_node=0;
	struct key_slot *slots_copy=NULL;
	node_t *new_owner=NULL, *old_owner=NULL;
	assert(thr->thread_id==0);
//	char uuid_str1[37]="", uuid_str2[37]="";
	if(n_node==0)
	{
		return;
	}
	slots_copy=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		memcpy(slots_copy+i, &(thr->ref_ks->slot_rts[i].slot), sizeof(struct key_slot));
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
			if(0!=node_compare(&thr->ref_ks->slot_rts[i].slot.owner, &slots_copy[i].owner))
			{
				if(changed_slot_start<0)
				{
					node_copy(&tmp_old_node, &thr->ref_ks->slot_rts[i].slot.owner);
					changed_slot_start=i;
				}
				changed_slot_end=i;
				node_copy(&tmp_new_node, &slots_copy[i].owner);
			}
			else if(changed_slot_end>0)
			{
				log_info(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE,
				 		"leader %s changes slot %d-%d owner from %s to %s.",
						thr->ref_ks->self.addr, changed_slot_start, changed_slot_end, 
						tmp_old_node.addr, tmp_new_node.addr);
				changed_slot_start=changed_slot_end=-1;
			}
		}
		propagate_slot_table_async(thr, slots_copy, KEYSPACE_SLOT_NUM);
	}
	free(slots_copy);
	slots_copy=NULL;
	return;
}
void remove_failed_nodes_from_key_route_table(struct ks_thread *thr, node_t *health_nodes, size_t n_node)
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
		if(!slot_is_my_thread(slot_id, thr->thread_id, thr->ref_ks->opts->nr_worker_threads)) continue;
		slot_rt=thr->ref_ks->slot_rts+slot_id;
		if(!node_compare(&slot_rt->slot.owner, &thr->ref_ks->self)) continue;
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
	}
	HASH_ITER(hh, hash_health_node, node, tmp_node)
	{
		HASH_DEL(hash_health_node, node);
		free(node);
	}
	if(n_modified_key>0)
	{
		log_fatal(thr->ref_ks->logger, MODULE_SWARMKV_KEYSPACE, 
				"thread %d modified %zu keys and removes %zu replicas of key route table for handling failed nodes.",
				thr->thread_id, n_modified_key, n_removed_replica);
	}
}
void watch_nodes_changes_on_success(void *result, void *arg)
{
	struct evhttp_request *req= (struct evhttp_request *)result;
	struct ks_thread *thr=(struct ks_thread*)arg;
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
		sscanf(tmp, "%d", &thr->consul_nodes_modify_idx);
		remove_failed_nodes_from_key_route_table(thr, health_nodes, n_node);
		if(thr->ref_ks->is_leader && thr->thread_id==0)
		{
			remove_failed_nodes_from_global_slot_table(thr, health_nodes, n_node);
		}
	}
	consul_watch_nodes_changes_async(thr);
}
void watch_nodes_changes_on_fail(enum e_future_error err, const char * what, void * arg)
{
	struct ks_thread *thr=(struct ks_thread*)arg;
	consul_watch_nodes_changes_async(thr);
}
void consul_watch_nodes_changes_async(struct ks_thread *thr)
{
	char url[SWARMKV_URL_MAX]="";

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1&index=%d&wait=10s", thr->ref_ks->db_name, thr->consul_nodes_modify_idx);
	struct future *f=future_create(__func__, watch_nodes_changes_on_success, watch_nodes_changes_on_fail, thr);
	struct http_request *req=http_request_new(thr->consul_client, __func__, f);
	http_request_make(req, EVHTTP_REQ_GET, url);
}

void ks_threads_init(struct swarmkv_keyspace *ks, struct event_base *evbases[], int nr_threads)
{
	int i=0, error=0;
	ks->threads=ALLOC(struct ks_thread, ks->opts->nr_worker_threads);
	struct ks_thread *thr=NULL;
	for(i=0; i<nr_threads; i++)
	{
		thr=ks->threads+i;
		thr->thread_id=i;
		thr->expires=timeouts_open(0, &error);
		thr->consul_client=http_client_new(ks->opts->consul_agent_host, ks->opts->consul_port, evbases[i], ks->logger);
		thr->ref_ks=ks;
		thr->evbase=evbases[i];
		consul_watch_slots_changes_async(thr);
		consul_watch_nodes_changes_async(thr);
	}
}
int keyslots_init(struct swarmkv_keyspace *ks)
{
	struct slot_runtime *slot_rt=NULL;
	struct key_slot *slot=NULL;
	for(int i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		slot=&(slot_rt->slot);
		slot->slot_id=i;
		node_copy(&slot->owner, &ks->self);
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
void swarmkv_keyspace_set_exec_cmd_handle(struct swarmkv_module *mod_keyspace, struct swarmkv *db)
{	
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ks->exec_cmd_handle=db;
	return;
}
#define MONITOR_KEY_EXPIRE_EVENT_EXPIRE	"keyspace-expire-cycle"
void swarmkv_keyspace_set_monitor_handle(struct swarmkv_module *mod_keyspace, struct swarmkv_module *mod_monitor)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ks->mod_monitor=mod_monitor;
	swarmkv_monitor_register_event(mod_monitor, MONITOR_KEY_EXPIRE_EVENT_EXPIRE);
	return;
}

struct swarmkv_module *swarmkv_keyspace_new(struct event_base *evbases[], int nr_threads, struct swarmkv_options *opts, const char *db_name, struct log_handle *logger, char **err)
{
	struct swarmkv_keyspace *ks=ALLOC(struct swarmkv_keyspace, 1);
	strncpy(ks->module.name, "keyspace", sizeof(ks->module.name));
	ks->module.mod_ctx=ks;
	strncpy(ks->db_name, db_name, sizeof(ks->db_name));
	node_init(&ks->self, opts->cluster_announce_ip, opts->cluster_announce_port);
	uuid_copy(ks->uuid, opts->bin_uuid);
	ks->opts=opts;
	ks->consul_agent_port=opts->consul_port;
    strncpy(ks->consul_agent_host, opts->consul_agent_host, sizeof(ks->consul_agent_host));

	ks->logger = logger;

	int ret=-1;
	ret=keyslots_init(ks);
	if(ret<0)
	{
		asprintf(err, "key slots init failed.");
        log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "key slots init failed.");
		goto error_out;
	}
	ks->threads=ALLOC(struct ks_thread, ks->opts->nr_worker_threads);
	struct ks_thread *thr=NULL;
	int error=0;
	for(int i=0; i<nr_threads; i++)
	{
		thr=ks->threads+i;
		thr->thread_id=i;
		thr->expires=timeouts_open(0, &error);
		thr->consul_client=http_client_new(ks->opts->consul_agent_host, ks->opts->consul_port, evbases[i], ks->logger);
		thr->ref_ks=ks;
		thr->evbase=evbases[i];
		consul_watch_slots_changes_async(thr);
		consul_watch_nodes_changes_async(thr);
	}

	return &(ks->module);

error_out:
	free(ks);
	return NULL;
}
void swarmkv_keyspace_start(struct swarmkv_module *mod_keyspace)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	if(ks->opts->dryrun)
	{
		log_info(ks->logger, MODULE_SWARMKV_KEYSPACE, "start to run on dry run mode.");
		return;
	}
	
	ks->http_server=http_server_new(ks->threads[0].evbase, &(ks->opts->health_check_port), ks);
	if(!ks->http_server)
	{
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "health check HTTP server start failed.");
		return;
	}
	if(ks->opts->health_check_announce_port==0)
	{
		ks->opts->health_check_announce_port=ks->opts->health_check_port;
	}
	int ret=0;
	ret=consul_service_register(ks);
	if(ret<0)
	{	
		log_fatal(ks->logger, MODULE_SWARMKV_KEYSPACE, "consul service register failed.");
		return;
	}
	if(ks->opts->run_for_leader_enabled)
	{
		consul_watch_leadership_changes_async(ks->threads+0);
	}
	return;
}
void swarmkv_keyspace_free(struct swarmkv_module* module)
{
	struct swarmkv_keyspace *ks = module2keyspace(module);

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
		HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp_key_entry)
		{
			HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
			key_entry_free(key_entry);
		}
	}
	for(i=0; i<ks->opts->nr_worker_threads; i++)
	{
		http_client_free(ks->threads[i].consul_client);
		ks->threads[i].consul_client=NULL;
		timeouts_close(ks->threads[i].expires);
		ks->threads[i].expires=NULL;
	}
	free(ks->threads);
	free(ks);
	return;
}
void swarmkv_keyspace_info(struct swarmkv_module *mod_keyspace, struct keyspace_info *info)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int i=0;
	struct slot_runtime *slot_rt=NULL;
	memset(info, 0, sizeof(struct keyspace_info));
	info->health_check_port=ks->opts->health_check_announce_port;
	info->keys=0;
	info->expires=0;
	info->slots=0;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		if(!node_compare(&slot_rt->slot.owner, &ks->self))
		info->slots++;
		info->keys+=HASH_COUNT(slot_rt->keyroute_table);
	}
	for(i=0; i<ks->opts->nr_worker_threads; i++)
	{
		info->expires+=timeouts_count(ks->threads[i].expires);
	}
	return;
}
enum cmd_exec_result keyslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	/* KEYSLOT key*/
	char *key=cmd->argv[1];
	int slot_id=key_hash_slot(key, strlen(key));
	*reply=swarmkv_reply_new_integer(slot_id);
	return FINISHED;
}
enum cmd_exec_result keyspace_setslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
	
	slot_rt->state=state;
	if(state!=STATE_STABLE)
	{
		node_copy(&slot_rt->rebalancing_peer, &peer_node);
	}

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
long long key_slot_merge(struct slot_runtime *slot_rt, const char *blob, size_t blob_sz, struct timeouts *expires)
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
			timeouts_del(expires, &key_entry->timeout_handle);
		}
		if(key_entry->abs_timeout)
		{
			timeout_init(&key_entry->timeout_handle, TIMEOUT_ABS);
			key_entry->is_expiring=1;				
			timeouts_add(expires, &key_entry->timeout_handle, key_entry->abs_timeout); 			
		}
	}
	if(mpack_tree_destroy(&tree)!= mpack_ok)
	{
		assert(0);
	}
	return new_added_entry_num;
}

enum cmd_exec_result keyspace_getkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd,  struct swarmkv_reply **reply)
{
/* KEYSPACE GETKEYSINSLOT slot */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	struct slot_runtime *slot_rt=NULL;
	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	slot_rt=ks->slot_rts+slot_id;
	char *blob=NULL;
	size_t blob_sz;
	key_slot_serialize(slot_rt, &blob, &blob_sz);
	*reply=swarmkv_reply_new_string(blob, blob_sz);
	free(blob);
	blob=NULL;
	return FINISHED;
}
static int ks_get_tid(struct swarmkv_keyspace *ks, int slot_id)
{
	int tid=swarmkv_keyspace_slot2tid(&ks->module, slot_id);
	int real_tid=__gettid(ks->exec_cmd_handle);
	assert(tid==real_tid);
	return tid;
}
enum cmd_exec_result keyspace_addkeystoslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* KEYSPACE ADDKEYSTOSLOT slot blob */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	long long new_added_entry_num=0;
	struct slot_runtime *slot_rt=NULL;

	const sds blob=cmd->argv[3];

	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	int tid=ks_get_tid(ks, slot_id);

	slot_rt=ks->slot_rts+slot_id;
	
	new_added_entry_num=key_slot_merge(slot_rt, blob, sdslen(blob), ks->threads[tid].expires);


	*reply=swarmkv_reply_new_integer(new_added_entry_num);
	return FINISHED;
}

enum cmd_exec_result keyspace_delslotkeys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* KEYSPACE DELSLOTKEYS slot */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);

	long long slot_id=-1;
	struct slot_runtime *slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	long long delete_num=0;

	if(0!=str2integer(cmd->argv[2], &slot_id) || slot_id>=KEYSPACE_SLOT_NUM)
	{
		*reply=swarmkv_reply_new_error(error_arg_not_valid_integer, cmd->argv[2]);
		return FINISHED;
	}
	int tid=ks_get_tid(ks, slot_id);
	slot_rt=ks->slot_rts+slot_id;
	struct ks_thread *thr=ks->threads+tid;

	HASH_ITER(hh, slot_rt->keyroute_table, key_entry, tmp)
	{
		HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
		if(key_entry->is_expiring)
		{
			timeouts_del(thr->expires, &key_entry->timeout_handle);
		}
		key_entry_free(key_entry);
		delete_num++;
	}

	*reply=swarmkv_reply_new_integer(delete_num);
	return FINISHED;
}
enum cmd_exec_result keyspace_countkeysinslot_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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

	key_num=HASH_COUNT(slot_rt->keyroute_table);

	*reply=swarmkv_reply_new_integer(key_num);
	return FINISHED;
}
enum KEYSPACE_OP
{
	KEYSPACE_RLIST,
	KEYSPACE_RADD,
	KEYSPACE_XRADD,
	KEYSPACE_DEL,
    KEYSPACE_RDEL,
	KEYSPACE_EXISTS
};

enum cmd_exec_result key_route_generic(struct swarmkv_keyspace *ks, enum KEYSPACE_OP op, const sds key, const node_t *new_replica, struct swarmkv_reply **reply)
{
	struct key_route_entry *key_entry=NULL;
	int slot_id=-1;
	struct slot_runtime *slot_rt=NULL;

	slot_id=key_hash_slot(key, sdslen(key));
	slot_rt=ks->slot_rts+slot_id;
	if(node_compare(&slot_rt->slot.owner, &ks->self) && slot_rt->state!=STATE_IMPORTING)
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
			key_entry_meet_replica(key_entry, ks->exec_cmd_handle);
			*reply=key_entry_list_replica_nodes(key_entry);
			break;
		case KEYSPACE_XRADD:
			if(key_entry)
			{
				key_entry_add_replica_node(key_entry, new_replica);
				*reply=key_entry_list_replica_nodes(key_entry);
				key_entry_meet_replica(key_entry, ks->exec_cmd_handle);
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
				key_entry_deletion_notification(key_entry, ks->exec_cmd_handle);
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
		case KEYSPACE_EXISTS:
			*reply=swarmkv_reply_new_integer(key_entry?1:0);
			break;
		default:
			assert(0);
			break;
	}
	return FINISHED;
}

UT_icd ut_swarmkv_reply_pointer_icd = {sizeof(struct swarmkv_reply *), NULL, NULL, NULL};
enum cmd_exec_result keyspace_keys_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* KEYSPACE KEYS tid pattern */

	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	size_t i=0, n_matched=0;
	struct slot_runtime *slot_rt=NULL;
	struct key_route_entry *key_entry=NULL, *tmp=NULL;
	int is_matched=0;
	UT_array *matched_replies=NULL;
	struct swarmkv_reply *r=NULL;
	int real_tid=__gettid(ks->exec_cmd_handle);
	int thread_id=atoll(cmd->argv[2]);
	assert(real_tid==thread_id);
	const sds pattern=cmd->argv[3];
	utarray_new(matched_replies, &ut_swarmkv_reply_pointer_icd);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		slot_rt=ks->slot_rts+i;
		if(node_compare(&slot_rt->slot.owner, &ks->self) && slot_rt->state!=STATE_IMPORTING)
		{
			continue;
		}
		if(!slot_is_my_thread(i, thread_id, ks->opts->nr_worker_threads))
		{
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
enum cmd_exec_result keyspace_rlist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE RLIST <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];
	
	ret=key_route_generic(ks, KEYSPACE_RLIST, key, NULL, reply);
	return ret;
}

enum cmd_exec_result keyspace_radd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
enum cmd_exec_result keyspace_xradd_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
enum cmd_exec_result keyspace_exists_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*KEYSPACE EXISTS <key>*/
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[2];
	
	ret=key_route_generic(ks, KEYSPACE_EXISTS, key, NULL, reply);
	return ret;
}
enum cmd_exec_result del_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/*DEL <key> */
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	enum cmd_exec_result ret=FINISHED;
	sds key=cmd->argv[1];
	ret=key_route_generic(ks, KEYSPACE_DEL, key, NULL, reply);
	return ret;
}

enum cmd_exec_result keyspace_rdel_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
	if(node_compare(&slot_rt->slot.owner, &ks->self) && slot_rt->state != STATE_IMPORTING)
	{
		*reply=swarmkv_reply_new_node(&(slot_rt->slot.owner), 1);
		return REDIRECT;
	}
	int tid=ks_get_tid(ks, slot_id);
	struct ks_thread *thr=ks->threads+tid;
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
					timeouts_del(thr->expires, &key_entry->timeout_handle);
				}
				timeout_init(&key_entry->timeout_handle, TIMEOUT_ABS);
				key_entry->is_expiring=1;
				key_entry->abs_timeout=now.tv_sec+seconds;
				timeouts_add(thr->expires, &key_entry->timeout_handle, key_entry->abs_timeout);				
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
				timeouts_del(thr->expires, &(key_entry->timeout_handle));
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
enum cmd_exec_result expire_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* EXPIRE key seconds */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_SET, cmd, reply);
	return ret;
}
enum cmd_exec_result ttl_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* TTL key */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_GET, cmd, reply);
	return ret;
}
enum cmd_exec_result persist_command(struct swarmkv_module *mod_keyspace, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* PERSIST key */
	enum cmd_exec_result ret=0;
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	ret=key_ttl_generic(ks, KEY_TTL_REMOVE, cmd, reply);
	return ret;

}
int swarmkv_keyspace_slot2tid(struct swarmkv_module *mod_keyspace, int slot_id)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	return slot_id%ks->opts->nr_worker_threads;
}
#define ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP 1000
void swarmkv_keyspace_periodic(struct swarmkv_module *mod_keyspace, int thread_id)
{
	struct swarmkv_keyspace *ks = module2keyspace(mod_keyspace);
	int real_tid=__gettid(ks->exec_cmd_handle);
	assert(real_tid==thread_id);
	struct ks_thread *thr=ks->threads+thread_id;

	int i=0;
	struct slot_runtime *slot_rt=NULL;
	struct timeout *t=NULL;	
	struct key_route_entry *key_entry=NULL;
	struct timespec start, end, now;
	int has_key_expired=0;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &start);
	clock_gettime(CLOCK_REALTIME, &now);


	timeouts_update(thr->expires, now.tv_sec);
	t=timeouts_get(thr->expires);
	while(t && i<ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP)
	{
		key_entry=container_of(t, struct key_route_entry, timeout_handle);
		assert(key_entry->is_expiring);
		slot_rt=ks->slot_rts+key_hash_slot(key_entry->key, sdslen(key_entry->key));
		HASH_DELETE(hh, slot_rt->keyroute_table, key_entry);
		key_entry_deletion_notification(key_entry, ks->exec_cmd_handle);
		key_entry_free(key_entry);
		t=timeouts_get(thr->expires);
		i++;
		has_key_expired=1;
	}
	
	clock_gettime(CLOCK_MONOTONIC_COARSE, &end);
	if(has_key_expired)
	{
		swarmkv_monitor_record_event(ks->mod_monitor, "keyspace-expire-cycle", timespec_diff_usec(&start, &end));
	}
}

