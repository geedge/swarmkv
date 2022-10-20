#include "swarmkv_net.h"
#include "swarmkv_utils.h"
#include "swarmkv_common.h"

#include "uthash.h"
#include "utarray.h"
#include "xxhash.h"
#include "log.h"

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
#include <netinet/tcp.h>//setsockopt TCP_NODELAY


#define	MODULE_SWAMRKV_NET	module_name_str("swarmkv.net")
#define	MSG_HDR_MAGIC	0x5210
enum MSG_TYPE
{
	MSG_TYPE_CMD,
	MSG_TYPE_REPLY
};

struct swarmkv_msg_hdr
{
	unsigned int magic;
	enum MSG_TYPE msg_type;	//0: cmd, 1: reply
	unsigned long long sequence;
	unsigned long long payload_len;
	struct swarmkv_node_addr source;
	//MessagePack payload
};
enum receive_state
{
	RECEIVING_HDR=0,
	RECEIVING_PAYLOAD
};
struct snet_conn
{
	struct swarmkv_node_addr peer_listen_addr;
	struct swarmkv_node_addr peer_connected_from_addr;
	int thread_id;
	int need_to_be_kill;
	enum receive_state recv_state;	
	struct swarmkv_msg_hdr receiving_hdr;
	int is_in_conn_table;
	struct evbuffer* buff_for_sending;
	pthread_mutex_t mutex;
	struct bufferevent *bev;
	evutil_socket_t fd;	
	struct snet_thread* ref_thr_ctx;
	UT_hash_handle hh;
};
struct timeout_ctx
{
	timeout_cb *cb;
	void *cb_arg;
	int is_persist;
	struct event *ev;
};


struct snet_rpc
{
	unsigned long long sequence;	
	struct swarmkv_node_addr peer;
	struct event *timeout_ev;
	on_reply_callback_t *cb;
	void* cb_arg;
	struct snet_thread *thr;
	UT_hash_handle hh;
};

struct snet_thread
{
	int thread_id;
	int is_dispatching;
	struct event_base *evbase;
	struct snet_conn* conn_table;

    unsigned int timeout;
	unsigned long long sequence_generator;
	struct snet_rpc *rpc_table;

	void* ref_logger;
	struct timeout_ctx *to_ctx;
	struct swarmkv_net* ref_net;
};
struct swarmkv_net
{
	size_t n_thread;
	uuid_t my_uuid;
	unsigned long long sequence_generator;
	int n_connection;
	struct swarmkv_node_addr self;
	struct snet_thread **threads;
	
	struct evconnlistener *listener;	

	void* logger;
	on_cmd_callback_t* on_cmd_cb;
	void* on_cmd_cb_arg;
};
static int get_net_thread_id(const char* peer_ip, size_t n_thread)
{
	int thread_id=XXH32(peer_ip, strlen(peer_ip), 171)%n_thread;
	return thread_id;
}

void snet_rpc_free(struct snet_rpc *rpc)
{
	event_del(rpc->timeout_ev);
	event_free(rpc->timeout_ev);
	HASH_DELETE(hh, rpc->thr->rpc_table, rpc);
	free(rpc);
}
void snet_rpc_timeout_callback(evutil_socket_t fd, short events, void *arg)
{	
	struct snet_rpc *rpc=(struct snet_rpc *)arg;
	rpc->cb(&rpc->peer, NULL, 0, rpc->cb_arg);
	snet_rpc_free(rpc);
}
struct snet_rpc *snet_rpc_lookup(struct snet_thread *thr, unsigned long long sequence)
{
	struct snet_rpc *rpc=NULL;
	HASH_FIND(hh, thr->rpc_table, &sequence, sizeof(sequence), rpc);
	return rpc;
}
void snet_rpc_process_reply(struct snet_rpc *rpc, void *data, size_t size)
{
	rpc->cb(&rpc->peer, data, size, rpc->cb_arg);
}
struct snet_rpc * snet_rpc_new(struct snet_thread *thr, struct swarmkv_node_addr *peer, unsigned long long sequence, on_reply_callback_t * cb, void * cb_arg)
{
	struct snet_rpc *rpc=ALLOC(struct snet_rpc, 1);
	rpc->sequence=thr->sequence_generator++;
	swarmkv_node_addr_copy(&rpc->peer, peer);
	rpc->cb=cb;
	rpc->cb_arg=cb_arg;
	rpc->thr=thr;
	struct timeval timeout={0, thr->timeout*1000};//500ms
	rpc->timeout_ev=event_new(thr->evbase, -1, 0, snet_rpc_timeout_callback, rpc);
	event_add(rpc->timeout_ev, &timeout);
	HASH_ADD(hh, thr->rpc_table, sequence, sizeof(rpc->sequence), rpc);
	return rpc;
}



enum TASK_TYPE
{
	TASK_TYPE_SEND,
	TASK_TYPE_RECEIVE
};

struct snet_task
{
	struct swarmkv_node_addr peer;
	enum TASK_TYPE task_type;
	enum MSG_TYPE msg_type;
	unsigned long long sequence;
	struct event *deferred_ev;	
	struct snet_thread *thr;
	void *data;
	size_t size;

	on_reply_callback_t *cb;
	void *cb_arg;
};
void snet_task_free(struct snet_task *task)
{
	free(task->data);
	task->data=NULL;
	event_free(task->deferred_ev);
	free(task);
	return;
}

void snet_send_in_evloop(struct snet_thread *thr, const struct swarmkv_node_addr *peer, unsigned long long sequence, enum MSG_TYPE msg_type, void *data, size_t size);

void snet_task_deferred_run(evutil_socket_t fd, short events, void *arg)
{
	struct snet_task *task=(struct snet_task *)arg;
	struct snet_thread *thr=task->thr;

	if(task->task_type==TASK_TYPE_SEND)
	{
		if(task->msg_type==MSG_TYPE_CMD)//expecting reply
		{
			struct snet_rpc *rpc=snet_rpc_new(thr, &task->peer, task->sequence, task->cb, task->cb_arg);
			snet_send_in_evloop(task->thr, &task->peer, rpc->sequence, task->msg_type, task->data, task->size);
		}
		else
		{
			snet_send_in_evloop(task->thr, &task->peer, task->sequence, task->msg_type, task->data, task->size);
		}
	}
	else//receive
	{
		struct snet_rpc *rpc=NULL;
		assert(task->sequence);
		if(task->msg_type==MSG_TYPE_REPLY)
		{
			rpc=snet_rpc_lookup(thr, task->sequence);
			if(rpc)
			{
				snet_rpc_process_reply(rpc, task->data, task->size);
				snet_rpc_free(rpc);
			}
			else
			{
				log_fatal(task->thr->ref_logger, MODULE_SWAMRKV_NET, "Unknown reply sequence %llu from %s:%u.",
										task->sequence,
										task->peer.ip_addr, task->peer.p2p_port);
			}

		}
		else
		{
			thr->ref_net->on_cmd_cb(&task->peer, task->sequence, task->data, task->size, thr->ref_net->on_cmd_cb_arg);
		}
	}
	snet_task_free(task);
	return;
}

struct snet_task *snet_task_new(struct snet_thread *thr, const struct swarmkv_node_addr* peer, enum TASK_TYPE task_type, enum MSG_TYPE msg_type, unsigned long long sequence, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg)
{
	struct snet_task *task=ALLOC(struct snet_task, 1);
	swarmkv_node_addr_copy(&task->peer, peer);
	task->deferred_ev=event_new(thr->evbase, -1, 0, snet_task_deferred_run, task);
	task->msg_type=msg_type;
	task->task_type=task_type;
	task->cb=cb;
	task->cb_arg=cb_arg;
	task->data=malloc(size);
	memcpy(task->data, data, size);
	task->size=size;
	task->sequence=sequence;
	task->thr=thr;
	event_add(task->deferred_ev, NULL);
	event_active(task->deferred_ev, 0, 0);
	return task;
}


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
static void snet_conn_table_remove(struct snet_conn* conn)
{
	struct snet_thread *thr=conn->ref_thr_ctx;
	assert(conn->is_in_conn_table==1);
	HASH_DEL(thr->conn_table, conn);
	conn->is_in_conn_table=0;
	return;
}
static void snet_conn_free(struct snet_conn* conn)
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
static void swarmkv_connection_kill(struct snet_conn* conn)
{
	conn->need_to_be_kill=1;
	bufferevent_trigger(conn->bev, EV_READ, BEV_TRIG_IGNORE_WATERMARKS);
	return;
}
static void snet_conn_set_peer(struct snet_conn* conn, struct swarmkv_node_addr *peer)
{
	memcpy(&conn->peer_listen_addr, peer, sizeof(conn->peer_listen_addr));
	return;
}

static void snet_conn_table_overwrite(struct snet_thread* thr, struct snet_conn* conn)
{
	struct snet_conn* tmp_conn=NULL;
	assert(conn->is_in_conn_table==0);
	HASH_FIND(hh, thr->conn_table, &(conn->peer_listen_addr), sizeof(struct swarmkv_node_addr), tmp_conn);
	if(tmp_conn)
	{
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET,
			"accept duplicated connection form peer %s:%u, previous connection is %s:%u.",
							conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port,
							tmp_conn->peer_connected_from_addr.ip_addr, tmp_conn->peer_connected_from_addr.p2p_port);
		snet_conn_table_remove(tmp_conn);
		swarmkv_connection_kill(tmp_conn);
		//The old connection (tmp_conn) will be freed in its peer_conn_read_cb 		
	}
	HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(conn->peer_listen_addr), conn);
	conn->is_in_conn_table=1;

	return;
}

static void peer_conn_write_cb(struct bufferevent *bev, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
	return;
}

static void peer_conn_read_cb(struct bufferevent *bev, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	struct snet_thread *thr=conn->ref_thr_ctx;
	assert(0==evbuffer_get_length(conn->buff_for_sending));
    struct evbuffer *input = bufferevent_get_input(bev);
	assert(bev==conn->bev);
	
	
	if(conn->need_to_be_kill)
	{
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connection %s:%u is killed",
									conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);
		assert(conn->is_in_conn_table==0);		
		snet_conn_free(conn);
		return;
	}
	const char *recv_buff=NULL;
	struct swarmkv_msg_hdr* hdr=NULL;
	while(1)
	{
		if(conn->recv_state==RECEIVING_HDR)
		{
			assert(conn->receiving_hdr.payload_len==0);
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
				log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "invalid message header magic from %s:%u",
										conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);
				if(conn->is_in_conn_table)
				{
					snet_conn_table_remove(conn);
				}
				snet_conn_free(conn);
				break;
			}
			if(!conn->is_in_conn_table)
			{
				snet_conn_set_peer(conn, &hdr->source);
				snet_conn_table_overwrite(thr, conn);			

				log_info(thr->ref_logger, MODULE_SWAMRKV_NET,
							"new peer connection from %s:%d has been set, which is connected from %s:%u.",
											conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port,
											conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);
			}
			conn->recv_state=RECEIVING_PAYLOAD;
			memcpy(&conn->receiving_hdr, hdr, sizeof(conn->receiving_hdr));
			evbuffer_drain(input, sizeof(struct swarmkv_msg_hdr));
		}
		if(conn->recv_state==RECEIVING_PAYLOAD)
		{
			recv_buff=(const char*)evbuffer_pullup(input, conn->receiving_hdr.payload_len);
			if(!recv_buff)//not enough buffer
			{
				bufferevent_setwatermark(conn->bev, EV_READ, conn->receiving_hdr.payload_len, 0);
				conn->recv_state=RECEIVING_PAYLOAD;
				break;
			}
			snet_task_new(thr, &(conn->peer_listen_addr), TASK_TYPE_RECEIVE, conn->receiving_hdr.msg_type, conn->receiving_hdr.sequence, recv_buff, conn->receiving_hdr.payload_len, NULL, NULL);
			evbuffer_drain(input, conn->receiving_hdr.payload_len);
			conn->recv_state=RECEIVING_HDR;
			memset(&conn->receiving_hdr, 0, sizeof(conn->receiving_hdr));
		}
	}
	return;
}

static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	struct snet_thread *thr=conn->ref_thr_ctx;
	
	int err = EVUTIL_SOCKET_ERROR();

	if (events & BEV_EVENT_ERROR || events & BEV_EVENT_EOF)
	{
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "%s:%u connection error, %d (%s).",
								conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port,
								err, evutil_socket_error_to_string(err));
	}
	if(conn->is_in_conn_table)
	{
		snet_conn_table_remove(conn);
	}
	snet_conn_free(conn);
	return;
}


void connect_peer_eventcb(struct bufferevent *bev, short events, void *arg)
{
	struct snet_conn* conn= (struct snet_conn*) arg;
	struct snet_thread* thr=conn->ref_thr_ctx;
	
	assert(bev==conn->bev);
	if (events & BEV_EVENT_CONNECTED)
	{
		int yes=1;
		evutil_socket_t fd=bufferevent_getfd(conn->bev);
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
		bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
		bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
		bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "peer %s:%u is connected.",
								conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port);
    }
	else if (events & BEV_EVENT_ERROR)
    {
		int err = EVUTIL_SOCKET_ERROR();
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connect to %s:%u failed %d (%s).",
								conn->peer_listen_addr.ip_addr, conn->peer_listen_addr.p2p_port,
								err, evutil_socket_error_to_string(err));
		snet_conn_table_remove(conn);
		snet_conn_free(conn);
    }
}

struct snet_conn* snet_conn_new_by_connecting(struct snet_thread* thr, const struct swarmkv_node_addr* dest)
{
	struct event_base *base=thr->evbase;
	
	struct snet_conn* conn=ALLOC(struct snet_conn, 1);
	conn->buff_for_sending=evbuffer_new();
//	evbuffer_enable_locking(conn->buff_for_sending, NULL);
	
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
struct snet_conn* snet_conn_new_by_fd(struct snet_thread *thr, evutil_socket_t fd, struct sockaddr * addr)
{
	struct snet_conn* conn=NULL;
	conn=ALLOC(struct snet_conn, 1);
	conn->buff_for_sending=evbuffer_new();
	int yes=1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
//	evbuffer_enable_locking(conn->buff_for_sending, NULL);
	evutil_make_socket_nonblocking(fd);
	sockaddr2string(addr, conn->peer_connected_from_addr.ip_addr, sizeof(conn->peer_connected_from_addr.ip_addr), &(conn->peer_connected_from_addr.p2p_port));
	conn->bev=bufferevent_socket_new(thr->evbase, fd, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);//BEV_OPT_UNLOCK_CALLBACKS|
	//use BEV_OPT_THREADSAFE option for safely access from threads that calling swarmkv_net_send.
	//? use BEV_OPT_UNLOCK_CALLBACKS option because swarmkv_net_send write buffer to output_buffer, and peer_conn_read_cb read input_buffer.
	conn->fd=fd;
	conn->thread_id=thr->thread_id;
	conn->ref_thr_ctx=thr;
	bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
	return conn;
}

void snet_conn_send(struct snet_conn *conn, const void *hdr, size_t hdr_sz, const void *payload, size_t payload_sz)
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
	return;
}

void snet_send(struct swarmkv_net* net, const struct swarmkv_node_addr* peer, enum MSG_TYPE msg_type, const void *data, size_t size, unsigned long long sequence, on_reply_callback_t *cb, void *cb_arg)
{
	int thread_id=0;
	thread_id=get_net_thread_id(peer->ip_addr, net->n_thread);
	struct snet_thread* thr=net->threads[thread_id];
	snet_task_new(thr, peer, TASK_TYPE_SEND, msg_type, sequence, data, size, cb, cb_arg);
	return;
}
void swarmkv_net_send_cmd(struct swarmkv_net* net, const struct swarmkv_node_addr* peer, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg)
{
	snet_send(net, peer, MSG_TYPE_CMD, data, size, 0, cb, cb_arg);
}
void swarmkv_net_send_reply(struct swarmkv_net* net, const struct swarmkv_node_addr* peer, const void *data, size_t size, unsigned long long sequence)
{	
	snet_send(net, peer, MSG_TYPE_REPLY, data, size, sequence, NULL, NULL);
}

void snet_send_in_evloop(struct snet_thread *thr, const struct swarmkv_node_addr *peer, unsigned long long sequence, enum MSG_TYPE msg_type, void *data, size_t size)
{
	struct snet_conn* conn=NULL;
	void* logger=thr->ref_net->logger;
	int err=0;

	HASH_FIND(hh, thr->conn_table, peer, sizeof(struct swarmkv_node_addr), conn);
	if(!conn)
	{
		conn=snet_conn_new_by_connecting(thr, peer);
		if(conn)
		{
			HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(struct swarmkv_node_addr), conn);
			conn->is_in_conn_table=1;
		}
		else
		{		
			err = EVUTIL_SOCKET_ERROR();
			log_fatal(logger, MODULE_SWAMRKV_NET, "connect to %s:%u failed, reason %d (%s).",
									peer->ip_addr, peer->p2p_port,
									err, evutil_socket_error_to_string(err));
			return;
		}
	}	
	struct swarmkv_msg_hdr msghdr;
	memset(&msghdr, 0, sizeof(msghdr));
	msghdr.magic=MSG_HDR_MAGIC;
	msghdr.msg_type=msg_type;
	msghdr.sequence=sequence;
	msghdr.payload_len=size;
	memcpy(&(msghdr.source), &(thr->ref_net->self), sizeof(msghdr.source));
	snet_conn_send(conn, &msghdr, sizeof(msghdr), data, size);
	return;
}


static void accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
	struct snet_thread *thr=NULL;
	struct snet_conn* conn=NULL;
	char peer_ip_str[MAX_IPv6_ADDR_STR_LEN]="";		
	sockaddr2string(addr, peer_ip_str, sizeof(peer_ip_str), NULL);
	int thread_id=get_net_thread_id(peer_ip_str, net->n_thread);
	thr=net->threads[thread_id];
	conn=snet_conn_new_by_fd(thr, fd, addr);

	log_debug(net->logger, MODULE_SWAMRKV_NET, "accept connection form from %s:%u.",
							conn->peer_connected_from_addr.ip_addr, conn->peer_connected_from_addr.p2p_port);

	return;
}
static void __accept_error_cb(struct evconnlistener *listener, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
    int err = EVUTIL_SOCKET_ERROR();
	log_fatal(net->logger, MODULE_SWAMRKV_NET, "accept peer connection failed %d (%s).",
							err, evutil_socket_error_to_string(err));
}

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, struct swarmkv_node_addr* self, void* logger, char **err)
{
	size_t i=0;
	struct swarmkv_net* net=NULL;	
    /* adds locking, only required if accessed from separate threads */
	evthread_use_pthreads();
	net=ALLOC(struct swarmkv_net, 1);
	net->logger=logger;
	net->n_thread=opts->nr_worker_threads;
	net->threads=ALLOC(struct snet_thread*, opts->nr_worker_threads);
	for(i=0; i<opts->nr_worker_threads; i++)
	{	
		net->threads[i]=ALLOC(struct snet_thread, 1);
		net->threads[i]->evbase=event_base_new();
		net->threads[i]->thread_id=i;
		net->threads[i]->timeout=opts->p2p_timeout_ms;
		net->threads[i]->ref_logger=logger;
		net->threads[i]->ref_net=net;
		net->threads[i]->sequence_generator=1;
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
	int sockfd=evconnlistener_get_fd(net->listener);
	socklen_t sin_len=0;
	if (getsockname(sockfd, (struct sockaddr *)&sin, &sin_len) < 0)
	{
		asprintf(err, "getsockname failed");
	}
	if(self->p2p_port==0)
	{
		self->p2p_port=ntohs(sin.sin_port);
	}
	assert(self->p2p_port==htons(sin.sin_port));
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
	struct snet_thread *thr=NULL;
	int i=0;
	
	for(i=0; i<net->n_thread; i++)
	{
		thr=net->threads[i];
		if(thr->is_dispatching)
		{
			event_base_loopexit(thr->evbase, NULL);
		}
	}
	return;
}
void swarmkv_net_free(struct swarmkv_net* net)
{
	int i=0;
	struct snet_thread *thr=NULL;
	struct snet_conn *conn=NULL, *tmp=NULL;
	struct snet_rpc *rpc=NULL, *tmp_rpc=NULL;
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
			snet_conn_table_remove(conn);
			snet_conn_free(conn);
		}
		HASH_ITER(hh, thr->rpc_table, rpc, tmp_rpc)
		{
			snet_rpc_free(rpc);
		}
		event_base_free(thr->evbase);
		free(thr);
	}
	free(net->threads);

	free(net);
	return;
}

void swarmkv_net_set_on_cmd_cb(struct swarmkv_net* net, on_cmd_callback_t on_cmd_cb, void* arg)
{
	net->on_cmd_cb_arg=arg;
	net->on_cmd_cb=on_cmd_cb;
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


