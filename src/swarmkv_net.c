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
#define	MSG_HDR_MAGIC	0x5210
struct swarmkv_msg_hdr
{
	unsigned long long magic;
	unsigned long long payload_len;
	struct swarmkv_node source;
	//MessagePack payload
};
enum receive_state
{
	RECEIVING_HDR=0,
	RECEIVING_PAYLOAD
};
struct swarmkv_peer_connection
{
	struct swarmkv_node peer;
	char peer_ip[INET6_ADDRSTRLEN];
	unsigned int peer_port;
	int thread_id;
	int need_to_be_kill;
	enum receive_state recv_state;
	size_t expect_payload_len;
	int is_in_conn_table;
	struct bufferevent *bev;
	struct evbuffer *output_buffer;
	evutil_socket_t fd;	
	struct swarmkv_net* ref_net;
	UT_hash_handle hh;
};

struct swarmkv_net
{
	int n_thread;
	char uuid[37];
	struct event_base ** evbases;
	struct evconnlistener *listener;
	
	struct swarmkv_peer_connection* conn_table;
	pthread_rwlock_t rwlock_conn_table;

	void* logger;
	net_recv_data_func* callback_func;
	void* callback_arg;
};
static void __dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_net *ctx = (struct swarmkv_net *)arg;
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
			*port=htons(((struct sockaddr_in *)sa)->sin_port);
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                   s, len);
			*port=htons(((struct sockaddr_in *)sa)->sin_port);
            break;
			
        default:
			assert(0);
            break;;
    }
	return;
}
void peer_conn_free(struct swarmkv_peer_connection* conn)
{
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
	free(conn);
	return;
}
void peer_conn_kill(struct swarmkv_peer_connection* conn)
{
	conn->need_to_be_kill=1;
	bufferevent_trigger(conn->bev, EV_READ, BEV_TRIG_IGNORE_WATERMARKS);
	return;
}
static void peer_conn_read_cb(struct bufferevent *bev, void *arg)
{
	struct swarmkv_peer_connection *conn=(struct swarmkv_peer_connection*)arg;
	struct swarmkv_peer_connection *tmp_conn=NULL;
	struct swarmkv_net *net=conn->ref_net;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

	if(conn->need_to_be_kill)
	{
		MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "connection %s:%u is killed",
									conn->peer_ip, conn->peer_port);
		peer_conn_free(conn);
		return;
	}
	const char *recv_buff=NULL;
	struct swarmkv_msg_hdr* hdr=NULL;
	
	if(conn->recv_state==RECEIVING_HDR)
	{
		assert(conn->expect_payload_len==0);
		recv_buff=evbuffer_pullup(input, sizeof(struct swarmkv_msg_hdr));
		if(!hdr)
		{
			bufferevent_setwatermark(bev, EV_READ, sizeof(struct swarmkv_msg_hdr), 0);
			conn->recv_state=RECEIVING_HDR;
			return;
		}
		hdr=recv_buff;
		if(hdr->magic!=MSG_HDR_MAGIC)
		{
			MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "invalid message header magic from %s:%u",
									conn->peer_ip, conn->peer_port);
			peer_conn_free(conn);
			return;
		}
		if(!conn->is_in_conn_table)
		{
			pthread_rwlock_wrlock(&net->rwlock_conn_table);
			HASH_FIND(hh, net->conn_table, &(hdr->source), sizeof(hdr->source), tmp_conn);
			if(tmp_conn)
			{
				MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, 
						"accept duplicated connection form peer %s:%u, previous connection is %s.",
										tmp_conn->peer_ip, tmp_conn->peer_port);
				HASH_DELETE(hh, net->conn_table, tmp_conn);				
				peer_conn_kill(tmp_conn);
				//The old connection (tmp_conn) will be freed in its peer_conn_read_cb 
			}
			conn->is_in_conn_table=1;
			memcpy(&conn->peer, &(hdr->source), sizeof(conn->peer));
			HASH_ADD(hh, net->conn_table, peer, &(conn->peer), conn);
			pthread_rwlock_unlock(&net->rwlock_conn_table);

			char uuid_str[MAX_UUID_STR_LEN];
			uuid_unparse_lower(conn->peer.bin_uuid, uuid_str);
			MESA_handle_runtime_log(net->logger, RLOG_LV_INFO, MODULE_SWAMRKV_NET, 
						"new peer connection from %s:%d (%s) has been set, source address is %s:%u.",
										conn->peer.ip_addr, conn->peer.p2p_port, uuid_str,
										conn->peer_ip, conn->peer_port);
		}
		conn->recv_state=RECEIVING_PAYLOAD;
		conn->expect_payload_len=hdr->payload_len;
		evbuffer_drain(input, sizeof(struct swarmkv_msg_hdr));
		hdr=NULL;
	}
	if(conn->recv_state==RECEIVING_PAYLOAD)
	{
		recv_buff=evbuffer_pullup(input, conn->expect_payload_len);
		if(!recv_buff)
		{
			bufferevent_setwatermark(bev, EV_READ, conn->expect_payload_len, 0);
			conn->recv_state=RECEIVING_PAYLOAD;
			return;
		}
		net->callback_func(&conn->peer, recv_buff, conn->expect_payload_len, net->callback_arg);
		conn->expect_payload_len=0;
		conn->recv_state=RECEIVING_HDR;
	}
}

static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg)
{
        if (events & BEV_EVENT_ERROR)
                perror("Error from bufferevent");
        if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
                bufferevent_free(bev);
        }
}

static void accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;	
	struct swarmkv_peer_connection* conn=NULL;
	struct swarmkv_node peer;
	
	int assign_thread=fd%net->n_thread;//do something crazy~	
	conn=ALLOC(struct swarmkv_peer_connection, 1);
	evutil_make_socket_nonblocking(fd);
	sockaddr2string(addr, conn->peer_ip, sizeof(conn->peer_ip), &(conn->peer_port));
	conn->bev=bufferevent_socket_new(net->evbases[assign_thread], fd, BEV_OPT_UNLOCK_CALLBACKS|BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);
	//use BEV_OPT_THREADSAFE option for safely access from threads that calling swarmkv_net_send.
	//? use BEV_OPT_UNLOCK_CALLBACKS option because swarmkv_net_send write buffer to output_buffer, and peer_conn_read_cb read input_buffer.
	conn->fd=fd;
	conn->thread_id=assign_thread;
	conn->ref_net=net;
	bufferevent_setcb(conn->bev, peer_conn_read_cb, NULL, peer_conn_event_cb, conn);

	MESA_handle_runtime_log(net->logger, RLOG_LV_DEBUG, MODULE_SWAMRKV_NET, "accept connection form peer %s:%u.",
							conn->peer_ip, conn->peer_port);

	return;
}
static void __accept_error_cb(struct evconnlistener *listener, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
	MESA_handle_runtime_log(net->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_NET, "accept peer connection failed %d (%s).",
							err, evutil_socket_error_to_string(err));		
}

int swarmkv_net_send(struct swarmkv_net* net, const struct swarmkv_node* dest, sds data)
{
	struct swarmkv_peer_connection* conn=NULL;
	pthread_rwlock_rdlock(&net->rwlock_conn_table);
	conn=HASH_FIND(hh, net->conn_table, dest, sizeof(struct swarmkv_node), conn);
	if(!conn)
	{
		connect;
	}
	pthread_rwlock_unlock(&net->rwlock_conn_table);
	//think about BEV_OPT_UNLOCK_CALLBACKS
	bufferevent_write();
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

	net->listener=evconnlistener_new_bind(net->evbases[0], accept_peer_connection_cb, net, LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));	
	evconnlistener_set_error_cb(net->listener, __accept_error_cb);
	return net;
}
void swarmkv_net_free(struct swarmkv_net* net)
{
	int i=0;
	struct swarmkv_peer_connection *conn=NULL, *tmp=NULL;
	HASH_ITER(hh, net->conn_table, conn, tmp)
	{
		HASH_DEL(net->conn_table, conn);
		peer_conn_free(conn);
	}
	for(i=0; i<net->n_thread; i++)
	{	
		event_base_loopexit(net->evbases[i], NULL);
		event_base_free(net->evbases[i]);
	}
	evconnlistener_free(net->listener)

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
	struct event * ev = event_new(net->evbases[thread_id], -1, EV_PERSIST, __dummy_event_handler, net);
	struct timeval timer_delay = {2, 0};
	evtimer_add(ev, &timer_delay);
	event_base_dispatch(net->evbases[thread_id]);
}
struct event_base * swarmkv_net_get_eventbase(struct swarmkv_net* net, int thread_id)
{
	return net->evbases[thread_id];
}

