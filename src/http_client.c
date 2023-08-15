#include "http_client.h"

#include "swarmkv_utils.h"
#include "swarmkv_limits.h"

#include "uthash.h"

#include "future_promise.h"


#define MODULE_HTTP_CLIENT	"HTTP_CLIENT"

struct http_request
{
	char symbol[SWARMKV_SYMBOL_MAX];
	long long request_id;
	struct evhttp_request *evhttpreq;
	struct evhttp_connection *evhttpconn; //tcp connection for the request
	struct future *f;
	struct http_client *ref_client;
	UT_hash_handle hh;
};
struct http_client
{
	unsigned short server_port;
	char server_host[32];
	long long req_id_generator;
	struct event_base *evbase;	//reference of keyspace evbase
	struct log_handle *logger;	//reference of logger
	struct http_request *request_table;
};
void http_request_on_close(struct evhttp_connection *conn, void *arg)
{
	return;
}
void http_request_callback(struct evhttp_request *evhttpreq, void *arg);
void http_request_error_callback(enum evhttp_request_error error, void *arg);
struct http_request *http_request_new(struct http_client *client, const char *symbol, struct future *f)
{
	struct http_request *req=ALLOC(struct http_request, 1);
	
	strncpy(req->symbol, symbol, sizeof(req->symbol));
	req->evhttpreq=evhttp_request_new(http_request_callback, req);
	req->f=f;
	req->ref_client=client;
	req->request_id=client->req_id_generator++;
	evhttp_request_set_error_cb(req->evhttpreq, http_request_error_callback);

	req->evhttpconn=evhttp_connection_base_new(client->evbase, NULL, client->server_host, client->server_port);

	evhttp_connection_set_timeout(req->evhttpconn, 30*60);//set to 30min for blocking query.
	evhttp_connection_set_closecb(req->evhttpconn, http_request_on_close, client);

	struct http_request *tmp=NULL;
	HASH_FIND(hh, client->request_table, symbol, strlen(symbol), tmp);
	assert(tmp==NULL);

	HASH_ADD(hh, client->request_table, symbol, strlen(symbol), req);
	
	return req;
}
void http_request_free(struct http_request *req)
{
	evhttp_connection_free(req->evhttpconn);
	//if(req->evhttpreq) evhttp_request_free(req->evhttpreq);
	future_destroy(req->f);
	
	free(req);
}
void http_request_error_callback(enum evhttp_request_error error, void *arg)
{
    if(error == EVREQ_HTTP_EOF)
	{
		return;
	}

	struct http_request *req=(struct http_request *)arg;
	struct promise *p=future_to_promise(req->f);
	promise_failed(p, FUTURE_ERROR_EXCEPTION, "http request error");
	http_request_free(req);
}
void http_request_callback(struct evhttp_request *evhttpreq, void *arg)
{
	struct http_request *req=(struct http_request *)arg;
	
	struct promise *p=future_to_promise(req->f);
	HASH_DEL(req->ref_client->request_table, req);
	if(!evhttpreq || !evhttp_request_get_response_code(evhttpreq))
	{
		int errcode = EVUTIL_SOCKET_ERROR();
		log_fatal(req->ref_client->logger, MODULE_HTTP_CLIENT, "%s failed: %s",
								req->symbol,
								evutil_socket_error_to_string(errcode));
		promise_failed(p, FUTURE_ERROR_EXCEPTION, "socket error");		
	}
	else
	{
		promise_success(p, evhttpreq);
	}
	//The request should be freed automatically after the http_request_done has finished executing.
	req->evhttpreq=NULL;
	http_request_free(req);
	return;
}
void http_request_make(struct http_request *req, enum evhttp_cmd_type cmd, const char *url)
{
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req->evhttpreq);
	evhttp_add_header(output_headers, "Host", req->ref_client->server_host);
	evhttp_add_header(output_headers, "Connection", "close");
	
	evhttp_make_request(req->evhttpconn, req->evhttpreq, cmd, url);
}

struct http_client *http_client_new(const char *host, unsigned short port, struct event_base *evbase, struct log_handle *logger)
{
	struct http_client *client=ALLOC(struct http_client, 1);
	client->server_port=port;
	strncpy(client->server_host, host, sizeof(client->server_host));
	client->logger=logger;
	client->evbase=evbase;

	return client;
}
void http_client_free(struct http_client *client)
{
	struct http_request *req=NULL, *tmp=NULL;
	HASH_ITER(hh, client->request_table, req, tmp)
	{
		HASH_DEL(client->request_table, req);
		http_request_free(req);
	}
	free(client);
	return;
}

struct http_block_request
{
	int response_code;
	struct event_base *evbase;
	struct evhttp_connection *evhttpconn;
	struct evhttp_request *req;	
	int error_happens;
	sds response_body;
};
void __http_blocking_request_cb(struct evhttp_request *req, void *arg)
{	
	struct http_block_request* client = (struct http_block_request*) arg;
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
struct evkeyvalq *http_request_get_output_headers(struct http_request *req)
{
	return evhttp_request_get_output_headers(req->evhttpreq);
}
struct evbuffer *http_request_get_output_buffer(struct http_request *req)
{
	return evhttp_request_get_output_buffer(req->evhttpreq);
}
int http_blocking_request(enum evhttp_cmd_type type, const char *host, unsigned int port, const char *url, const sds req_body, sds *resp_body)
{
	struct http_block_request client_data;
	struct http_block_request* client=&client_data;
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