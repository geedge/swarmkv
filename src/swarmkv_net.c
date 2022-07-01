#include "swarmkv_net.h"
#include "swarmkv_utils.h"
#include "swarmkv_common.h"

#include "uthash.h"
#include "utarray.h"
#include "xxhash.h"

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
#include <pthread.h>
#include <unistd.h> //usleep


#define	MODULE_SWAMRKV_NET	module_name_str("swarmkv.net")
#define	MSG_HDR_MAGIC	0x5210
struct swarmkv_msg_hdr
{
	unsigned long long magic;
	unsigned long long payload_len;
	struct swarmkv_node_addr source;
	//MessagePack payload
};
enum receive_state
{
	RECEIVING_HDR=0,
	RECEIVING_PAYLOAD
};
struct swarmkv_connection
{
	struct swarmkv_node_addr peer_listen_addr;
	struct swarmkv_node_addr peer_connected_from_addr;
	int thread_id;
	int need_to_be_kill;
	enum receive_state recv_state;
	size_t expect_payload_len;
	int is_in_conn_table;
	struct evbuffer* buff_for_sending;
	struct bufferevent *bev;
	evutil_socket_t fd;	
	struct swarmkv_net_thread_ctx* ref_thr_ctx;
	UT_hash_handle hh;
};
struct timeout_ctx
{
	timeout_cb *cb;
	void *cb_arg;
	int is_persist;
	struct event *ev;
};

struct swarmkv_net_thread_ctx
{
	int thread_id;
	int is_dispatching;
	struct event_base *evbase;
	struct swarmkv_connection* conn_table;
//	pthread_rwlock_t rwlock;
	pthread_mutex_t mutex;
	void* ref_logger;
	struct timeout_ctx *to_ctx;
	struct swarmkv_net* ref_net;
};
struct swarmkv_net
{
	size_t n_thread;
	int n_connection;
	struct swarmkv_node_addr self;
	struct swarmkv_net_thread_ctx **threads;
	
	struct evconnlistener *listener;	

	void* logger;
	net_recv_data_func* callback_func;
	void* callback_arg;
};
static void __dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
//	struct swarmkv_net *ctx = (struct swarmkv_net *)arg;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return;
}
static void sockaddr2string(struct sockaddr * sa, char* s, size_t len, unsigned int *port)
{
    switch(sa->sa_family)
	{
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                    s, len);
			if(port)
			{
				*port=htons(((struct sockaddr_in *)sa)->sin_port);
			}
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                   s, len);
			if(port)
			{
				*port=htons(((struct sockaddr_in *)sa)->sin_port);
			}
            break;
			
        default:
			assert(0);
            break;;
    }
	return;
}
static void swarmkv_net_conn_table_remove(struct swarmkv_connection* conn)
{
	struct swarmkv_net_thread_ctx *thr=conn->ref_thr_ctx;
	assert(conn->is_in_conn_table==1);
	HASH_DEL(thr->conn_table, conn);
	conn->is_in_conn_table=0;
	return;
}
static void swarmkv_connection_free(struct swarmkv_connection* conn)
{
	evbuffer_free(conn->buff_for_sending);
	conn->buff_for_sending=NULL;
	if(conn->bev)
	{
		bufferevent_disable(conn->bev, EV_READ | EV_WRITE);
		bufferevent_free(conn->bev);
	}
	if(conn->fd>0)
	{
		evutil_closesocket(conn->fd);
		conn->fd=-1;
	}
	assert(!conn->is_in_conn_table);
	free(conn);
	return;
}
static void swarmkv_connection_kill(struct swarmkv_connection* conn)
{
	conn->need_to_be_kill=1;
	bufferevent_trigger(conn->bev, EV_READ, BEV_TRIG_IGNORE_WATERMARKS);
	return;
}
static void swarmkv_connection_set_peer(struct swarmkv_connection* conn, struct swarmkv_node_addr *peer)
{
	memcpy(&conn->peer_listen_addr, peer, sizeof(conn->peer_listen_addr));
	return;
}

static void swarmkv_net_conn_table_overwrite(struct swarmkv_net_thread_ctx* thr, struct swarmkv_connection* conn)
{
	struct swarmkv_connection* tmp_conn=NULL;
	assert(conn->is_in_conn_table==0);
	HASH_FIND(hh, thr->conn_table, &(conn->peer_listen_addr), sizeof(struct swarmkv_node_addr), tmp_conn);
	if(tmp_conn)
	{
		MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, 
			"accept duplicated connection form peer %s:%u, previous connection is %s:%u.",
							conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port,
							tmp_conn->peer_connected_from_addr.ip_addr, tmp_conn->peer_connected_from_addr.p2p_port);
		swarmkv_net_conn_table_remove(tmp_conn);
		swarmkv_connection_kill(tmp_conn);
		//The old connection (tmp_conn) will be freed in its peer_conn_read_cb 		
	}
	HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(conn->peer_listen_addr), conn);
	conn->is_in_conn_table=1;

	return;
}
/*
static void peer_conn_write_cb(struct bufferevent *bev, void *arg)
{
	struct swarmkv_connection *conn=(struct swarmkv_connection*)arg;
	bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
	return;
}
*/
static void peer_conn_read_cb(struct bufferevent *bev, void *arg)
{
	struct swarmkv_connection *conn=(struct swarmkv_connection*)arg;
	struct swarmkv_net_thread_ctx *thr=conn->ref_thr_ctx;
	assert(0==evbuffer_get_length(conn->buff_for_sending));
    struct evbuffer *input = bufferevent_get_input(bev);
	assert(bev==conn->bev);
	
	
	if(conn->need_to_be_kill)
	{
		MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "connection %s:%u is killed",
									conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);
		assert(conn->is_in_conn_table==0);		
		swarmkv_connection_free(conn);
		return;
	}
	const char *recv_buff=NULL;
	struct swarmkv_msg_hdr* hdr=NULL;
	while(1)
	{
		if(conn->recv_state==RECEIVING_HDR)
		{
			assert(conn->expect_payload_len==0);
			recv_buff=(const char*) evbuffer_pullup(input, sizeof(struct swarmkv_msg_hdr));
			if(!recv_buff)
			{
				bufferevent_setwatermark(bev, EV_READ, sizeof(struct swarmkv_msg_hdr), 0);
				conn->recv_state=RECEIVING_HDR;
				break;
			}
			hdr=(struct swarmkv_msg_hdr*)recv_buff;
			if(hdr->magic!=MSG_HDR_MAGIC)
			{
				MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "invalid message header magic from %s:%u",
										conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);									
				pthread_mutex_lock(&thr->mutex);
				swarmkv_net_conn_table_remove(conn);
				pthread_mutex_unlock(&thr->mutex);
				
				swarmkv_connection_free(conn);
				break;
			}
			if(!conn->is_in_conn_table)
			{
				swarmkv_connection_set_peer(conn, &hdr->source);
				pthread_mutex_lock(&thr->mutex);
				swarmkv_net_conn_table_overwrite(thr, conn);			
				pthread_mutex_unlock(&thr->mutex);

				MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_INFO, MODULE_SWAMRKV_NET, 
							"new peer connection from %s:%d has been set, which is connected from %s:%u.",
											conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port,
											conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);
			}
			conn->recv_state=RECEIVING_PAYLOAD;
			conn->expect_payload_len=hdr->payload_len;
			evbuffer_drain(input, sizeof(struct swarmkv_msg_hdr));
			hdr=NULL;
		}
		if(conn->recv_state==RECEIVING_PAYLOAD)
		{
			recv_buff=(const char*)evbuffer_pullup(input, conn->expect_payload_len);
			if(!recv_buff)//not enough buffer
			{
				bufferevent_setwatermark(conn->bev, EV_READ, conn->expect_payload_len, 0);
				conn->recv_state=RECEIVING_PAYLOAD;
				break;
			}
			thr->ref_net->callback_func(&(conn->peer_listen_addr), recv_buff, conn->expect_payload_len, thr->ref_net->callback_arg);
			evbuffer_drain(input, conn->expect_payload_len);
			conn->expect_payload_len=0;
			conn->recv_state=RECEIVING_HDR;
		}
	}
	return;
}

static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct swarmkv_connection *conn=(struct swarmkv_connection*)arg;
	struct swarmkv_net_thread_ctx *thr=conn->ref_thr_ctx;
	
	int err = EVUTIL_SOCKET_ERROR();

	if (events & BEV_EVENT_ERROR || events & BEV_EVENT_EOF)
	{
		MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_DEBUG, MODULE_SWAMRKV_NET, "%s:%u connection error, %d (%s).",
								conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port,
								err, evutil_socket_error_to_string(err));
	}
	pthread_mutex_lock(&thr->mutex);
	if(conn->is_in_conn_table)
	{
		swarmkv_net_conn_table_remove(conn);
	}
	pthread_mutex_unlock(&thr->mutex);
	swarmkv_connection_free(conn);
	return;
}


void connect_peer_eventcb(struct bufferevent *bev, short events, void *arg)
{
	struct swarmkv_connection* conn= (struct swarmkv_connection*) arg;
	struct swarmkv_net_thread_ctx* thr=conn->ref_thr_ctx;
	
	assert(bev==conn->bev);
	if (events & BEV_EVENT_CONNECTED)
	{
		//conn->fd=bufferevent_getfd(conn->bev);
		bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
		bufferevent_setcb(conn->bev, peer_conn_read_cb, NULL, peer_conn_event_cb, conn);
		bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
		MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_DEBUG, MODULE_SWAMRKV_NET, "peer %s:%u is connected.",
								conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port);
    }
	else if (events & BEV_EVENT_ERROR)
    {
		int err = EVUTIL_SOCKET_ERROR();
		MESA_handle_runtime_log(thr->ref_logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "connect to %s:%u failed %d (%s).",
								conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port,
								err, evutil_socket_error_to_string(err));
		pthread_mutex_lock(&thr->mutex);
		swarmkv_net_conn_table_remove(conn);
		pthread_mutex_unlock(&thr->mutex);		
		swarmkv_connection_free(conn);
    }
}

struct swarmkv_connection* swarmkv_connection_new_by_connect(struct swarmkv_net_thread_ctx* thr, const struct swarmkv_node_addr* dest)
{
	struct event_base *base=thr->evbase;
	
	struct swarmkv_connection* conn=ALLOC(struct swarmkv_connection, 1);
	conn->buff_for_sending=evbuffer_new();
	evbuffer_enable_locking(conn->buff_for_sending, NULL);
	//http://www.wangafu.net/~nickm/libevent-book/Ref6_bufferevent.html
	conn->bev=bufferevent_socket_new(base, -1, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);//BEV_OPT_UNLOCK_CALLBACKS|
	bufferevent_setcb(conn->bev, NULL, NULL, connect_peer_eventcb, conn);
	swarmkv_node_addr_copy(&conn->peer_listen_addr, dest);
	conn->thread_id=thr->thread_id;
	conn->ref_thr_ctx=thr;
	swarmkv_node_addr_copy(&conn->peer_connected_from_addr, dest);
	
	struct sockaddr_in sa4;
	struct sockaddr_in6 sa6;
	if(NULL==strstr(dest->ip_addr, ":"))//ipv6
	{		
		memset(&sa4, 0, sizeof(sa4));
		sa4.sin_family = AF_INET;
		sa4.sin_addr.s_addr = inet_addr(dest->ip_addr);
		sa4.sin_port = htons(dest->p2p_port);
	    if (bufferevent_socket_connect(conn->bev, (struct sockaddr *)&sa4, sizeof(sa4)) < 0)
		{
	        bufferevent_free(conn->bev);		
	        goto error_out;
	    }
	}
	else
	{
		memset(&sa6, 0, sizeof(sa6));
		sa6.sin6_family = AF_INET6;
	    inet_pton(AF_INET6, dest->ip_addr, &sa6.sin6_addr);
	    sa6.sin6_port  = htons(dest->p2p_port);
		if (bufferevent_socket_connect(conn->bev, (struct sockaddr *)&sa6, sizeof(sa6)) < 0)
		{
			bufferevent_free(conn->bev);		
			goto error_out;
		}
	}
	return conn;

error_out:
	evbuffer_free(conn->buff_for_sending);
	conn->buff_for_sending=NULL;
	free(conn);
	return NULL;

}
struct swarmkv_connection* swarmkv_connection_new_by_fd(struct swarmkv_net_thread_ctx *thr, evutil_socket_t fd, struct sockaddr * addr)
{
	struct swarmkv_connection* conn=NULL;
	conn=ALLOC(struct swarmkv_connection, 1);
	conn->buff_for_sending=evbuffer_new();
	evbuffer_enable_locking(conn->buff_for_sending, NULL);
	evutil_make_socket_nonblocking(fd);
	sockaddr2string(addr, conn->peer_connected_from_addr.ip_addr, sizeof(conn->peer_connected_from_addr.ip_addr), &(conn->peer_connected_from_addr.p2p_port));
	conn->bev=bufferevent_socket_new(thr->evbase, fd, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);//BEV_OPT_UNLOCK_CALLBACKS|
	//use BEV_OPT_THREADSAFE option for safely access from threads that calling swarmkv_net_send.
	//? use BEV_OPT_UNLOCK_CALLBACKS option because swarmkv_net_send write buffer to output_buffer, and peer_conn_read_cb read input_buffer.
	conn->fd=fd;
	conn->thread_id=thr->thread_id;
	conn->ref_thr_ctx=thr;
	bufferevent_setcb(conn->bev, peer_conn_read_cb, NULL, peer_conn_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
	return conn;
}

void swarmkv_connection_send(struct swarmkv_connection *conn, const void *hdr, size_t hdr_sz, const void *payload, size_t payload_sz)
{
	struct evbuffer* output_buffer=NULL;
	output_buffer=bufferevent_get_output(conn->bev);
	
	if(output_buffer)
	{
		evbuffer_add(output_buffer, hdr, hdr_sz);
		evbuffer_add(output_buffer, payload, payload_sz);
	}
	else
	{
		evbuffer_add(conn->buff_for_sending, hdr, hdr_sz);
		evbuffer_add(conn->buff_for_sending, payload, payload_sz);

	}
	/*
	bufferevent_lock(conn->bev);
	int ret=0;
	ret=bufferevent_write(conn->bev, hdr, hdr_sz);
	assert(ret==0);
	ret=bufferevent_write(conn->bev, payload, payload_sz);
	assert(ret==0);
	bufferevent_unlock(conn->bev);
*/
	return;
}
static int get_net_thread_id(const char* peer_ip, size_t n_thread)
{
	int thread_id=XXH32(peer_ip, strlen(peer_ip), 171)%n_thread;
	return thread_id;
}

int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node_addr* dest, const sds data)
{
	struct swarmkv_connection* conn=NULL;
	int err=0, thread_id=0;
	thread_id=get_net_thread_id(dest->ip_addr, net->n_thread);
	struct swarmkv_net_thread_ctx* thr=net->threads[thread_id];
	void* logger=net->logger;

	pthread_mutex_lock(&thr->mutex);
	HASH_FIND(hh, thr->conn_table, dest, sizeof(struct swarmkv_node_addr), conn);
	if(!conn)
	{
		conn=swarmkv_connection_new_by_connect(thr, dest);
		if(conn)
		{
			HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(struct swarmkv_node_addr), conn);
			conn->is_in_conn_table=1;
		}
		else
		{		
			err = EVUTIL_SOCKET_ERROR();
			MESA_handle_runtime_log(logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "connect to %s:%u failed, reason %d (%s).",
									dest->ip_addr, dest->p2p_port,
									err, evutil_socket_error_to_string(err));
			pthread_mutex_unlock(&thr->mutex);
			return -1;
		}
	}	
	pthread_mutex_unlock(&thr->mutex);

	struct swarmkv_msg_hdr msghdr;
	memset(&msghdr, 0, sizeof(msghdr));
	msghdr.magic=MSG_HDR_MAGIC;
	msghdr.payload_len=sdslen(data);
	memcpy(&(msghdr.source), &(net->self), sizeof(msghdr.source));
	swarmkv_connection_send(conn, &msghdr, sizeof(msghdr), data, sdslen(data));
	return 0;
}
static void accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
	struct swarmkv_net_thread_ctx *thr=NULL;
	struct swarmkv_connection* conn=NULL;
	char peer_ip_str[MAX_IPv6_ADDR_STR_LEN]="";		
	sockaddr2string(addr, peer_ip_str, sizeof(peer_ip_str), NULL);
	int thread_id=get_net_thread_id(peer_ip_str, net->n_thread);
	thr=net->threads[thread_id];
	conn=swarmkv_connection_new_by_fd(thr, fd, addr);

	MESA_handle_runtime_log(net->logger, RLOG_LV_DEBUG, MODULE_SWAMRKV_NET, "accept connection form peer %s:%u.",
							conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);

	return;
}
static void __accept_error_cb(struct evconnlistener *listener, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
    int err = EVUTIL_SOCKET_ERROR();
	MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "accept peer connection failed %d (%s).",
							err, evutil_socket_error_to_string(err));		
}

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, const struct swarmkv_node_addr* self, void* logger, char **err)
{
	size_t i=0;
	struct swarmkv_net* net=NULL;	
    /* adds locking, only required if accessed from separate threads */
	evthread_use_pthreads();
	net=ALLOC(struct swarmkv_net, 1);
	net->logger=logger;
	net->n_thread=opts->nr_worker_threads;
	net->threads=ALLOC(struct swarmkv_net_thread_ctx*, opts->nr_worker_threads);
	for(i=0; i<opts->nr_worker_threads; i++)
	{	
		net->threads[i]=ALLOC(struct swarmkv_net_thread_ctx, 1);
		net->threads[i]->evbase=event_base_new();
		pthread_mutex_init(&(net->threads[i]->mutex), NULL);
		net->threads[i]->thread_id=i;
		net->threads[i]->ref_logger=logger;
		net->threads[i]->ref_net=net;
	}
	memcpy(&(net->self), self, sizeof(struct swarmkv_node_addr));


	//Refer to http://www.wangafu.net/~nickm/libevent-book/Ref8_listener.html
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);	// Listen on 0.0.0.0
	sin.sin_port = htons((unsigned short)self->p2p_port);
	net->listener=evconnlistener_new_bind(net->threads[0]->evbase, accept_peer_connection_cb, net, 
										LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));
	if(!net->listener)
	{
		asprintf(err, "listen on port %u failed", self->p2p_port);
		free(net);
		return NULL;
	}
	evconnlistener_set_error_cb(net->listener, __accept_error_cb);
	
	return net;
}


static void timeout_ctx_free(struct timeout_ctx *ctx)
{
	event_del(ctx->ev);
	event_free(ctx->ev);
	free(ctx);
	return;
}
static void wrap_timeout_handler(evutil_socket_t fd, short what, void * arg)
{
	struct timeout_ctx *ctx=(struct timeout_ctx *)arg;
	ctx->cb(ctx->cb_arg);
	if(!ctx->is_persist)
	{
		timeout_ctx_free(ctx);
	}
	return;
}

void swarmkv_net_timer_add(struct swarmkv_net *net, int thread_id, int msec, int is_persist, timeout_cb *cb, void *cb_arg)
{
	struct timeout_ctx *ctx=ALLOC(struct timeout_ctx, 1);
	ctx->cb=cb;
	ctx->is_persist=is_persist;
	ctx->cb_arg=cb_arg;
	ctx->ev=event_new(net->threads[thread_id]->evbase, -1, EV_PERSIST, wrap_timeout_handler, ctx);

	struct timeval timer_delay = {msec/1000, (msec%1000)*1000};
	evtimer_add(ctx->ev, &timer_delay);
	assert(net->threads[thread_id]->to_ctx==NULL);
	net->threads[thread_id]->to_ctx=ctx;
}
void swarmkv_net_stop(struct swarmkv_net* net)
{
	struct swarmkv_net_thread_ctx *thr=NULL;
	int i=0;
	
	for(i=0; i<net->n_thread; i++)
	{
		thr=net->threads[i];
		if(thr->is_dispatching)
		{
			event_base_loopexit(thr->evbase, NULL);
			while(!event_base_got_exit(thr->evbase) )
			{
				usleep(100);
			}
		}
	}
	return;
}
void swarmkv_net_free(struct swarmkv_net* net)
{
	int i=0;
	struct swarmkv_net_thread_ctx *thr=NULL;
	struct swarmkv_connection *conn=NULL, *tmp=NULL;

	evconnlistener_free(net->listener);
	for(i=0; i<net->n_thread; i++)
	{	
		thr=net->threads[i];
		if(thr->to_ctx)
		{
			timeout_ctx_free(thr->to_ctx);
			thr->to_ctx=NULL;
		}
		net->threads[i]=NULL;
		HASH_ITER(hh, thr->conn_table, conn, tmp)
		{
			swarmkv_net_conn_table_remove(conn);
			swarmkv_connection_free(conn);
		}		
		event_base_free(thr->evbase);
		free(thr);
	}
	free(net->threads);

	free(net);
	return;
}

void swarmkv_net_set_recv_cb(struct swarmkv_net* net, net_recv_data_func recv_cb, void* arg)
{
	assert(!net->callback_func);
	net->callback_func=recv_cb;
	net->callback_arg=arg;
	return;
}
void swarmkv_net_dispath(struct swarmkv_net* net, int thread_id)
{
	struct event * ev = event_new(net->threads[thread_id]->evbase, -1, EV_PERSIST, __dummy_event_handler, net);
	struct timeval timer_delay = {2, 0};
	net->threads[thread_id]->is_dispatching=1;
	evtimer_add(ev, &timer_delay);
	event_base_dispatch(net->threads[thread_id]->evbase);
	
	event_del(ev);
	event_free(ev);
	return;
}


