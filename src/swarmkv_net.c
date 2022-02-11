#include "swarmkv_net.h"
#include "swarmkv_utils.h"

#include "uthash.h"
#include "utarray.h"

#include <MESA/MESA_handle_logger.h>


#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define	MODULE_SWAMRKV_NET	"swarmkv.net"

struct swarmkv_peer_connection
{
	struct swarmkv_node peer;
	struct bufferevent *buf_ev;
	struct evbuffer *output_buffer;	
	UT_hash_handle hh;
};

struct swarmkv_net
{
	int n_thread;
	struct swarmkv_peer_connection* conn_table;
	struct event_base ** evbases;
	pthread_mutex_t mutex;
	void* logger;
};
static void __dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_net *ctx = (struct swarmkv_net *)arg;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return;
}

static void __accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
	/*
	bufferevent_socket_new(__ev_base, fd, BEV_OPT_DEFER_CALLBACKS | BEV_OPT_THREADSAFE );
	__conn_private->fd = fd;

	bufferevent_disable(__conn_private->bev, EV_READ | EV_WRITE);

	if (!__conn_private->bev)
	{
		TFE_LOG_ERROR(__STREAM_LOGGER(stream), "Failed at creating bufferevent for fd %d", fd);
		goto __errout;
	}

	bufferevent_setcb(__conn_private->bev, __stream_bev_passthrough_readcb,
			__stream_bev_passthrough_writecb, __stream_bev_passthrough_eventcb, stream);
	*/
}
static void __accept_error_cb(struct evconnlistener *listener, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
	MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "accept peer connection failed %d (%s).",
							err, evutil_socket_error_to_string(err));		
//    event_base_loopexit(base, NULL);
}

int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node* dest, const char* data, size_t datalen)
{
	return 0;
}
struct swarmkv_net* swarmkv_net_new(unsigned int listen_port, int thread_num, void* logger)
{
	int i=0;
	struct swarmkv_net* net=NULL;	
    /* adds locking, only required if accessed from separate threads */
	evthread_use_pthreads();
	net=ALLOC(struct swarmkv_net, 1);
	net->n_thread=thread_num;
	net->evbases=ALLOC(struct event_base*, thread_num);
	for(i=0; i<thread_num; i++)
	{	
		net->evbases[i] = event_base_new();
	}
	//Refer to http://www.wangafu.net/~nickm/libevent-book/Ref8_listener.html
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);	// Listen on 0.0.0.0
	sin.sin_port = htons((unsigned short)listen_port);

	struct evconnlistener *listener;
	listener=evconnlistener_new_bind(net->evbases[0], __accept_peer_connection_cb, net, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));	
	evconnlistener_set_error_cb(listener, __accept_error_cb);
	return net;
}

void swarmkv_net_set_recv_cb(struct swarmkv_net* net, net_recv_data_func recv_cb, void* arg)
{
	return;
}
void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id)
{
	struct event * ev = event_new(net->evbases[thread_id], -1, EV_PERSIST, __dummy_event_handler, net);
	struct timeval timer_delay = {2, 0};
	evtimer_add(ev, &timer_delay);
	event_base_dispatch(net->evbases[thread_id]);
}
struct event_base * swarmkv_net_get_eventbase(struct swarmkv_net* net, int thread_id)
{
	return net->evbases[thread_id];
}
void swarmkv_net_free(struct swarmkv_net* net)
{
	return;
}


