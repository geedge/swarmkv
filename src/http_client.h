#pragma once
#include "log.h"
#include "sds.h"
#include "future_promise.h"

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/http.h>

struct http_client *http_client_new(const char *server_host, unsigned short server_port, struct event_base *evbase, struct log_handle *logger);
void http_client_free(struct http_client *client);

struct http_request *http_request_new(struct http_client *client, const char *symbol, struct future *f);
struct evkeyvalq *http_request_get_output_headers(struct http_request *req);
struct evbuffer *http_request_get_output_buffer(struct http_request *req);
void http_request_make(struct http_request *req, enum evhttp_cmd_type cmd, const char *url);
void http_request_free(struct http_request *req);
int http_blocking_request(enum evhttp_cmd_type type, const char *host, unsigned int port, const char *url, const sds req_body, sds *resp_body);