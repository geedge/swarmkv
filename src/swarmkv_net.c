#include "swarmkv_net.h"
#include "swarmkv_utils.h"
#include "swarmkv_common.h"
#include "swarmkv_monitor.h"

#include "uthash.h"
#include "utarray.h"
#include "xxhash.h"
#include "log.h"

//#include <valgrind/memcheck.h> //VALGRIND_CHECK_VALUE_IS_DEFINED

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
	node_t source;
	//MessagePack payload
};
enum receive_state
{
	RECEIVING_HDR=0,
	RECEIVING_PAYLOAD
};
struct snet_conn
{
	node_t peer_listen_addr;
	node_t connected_from;
	int thread_id;
	int need_to_be_kill;
	enum receive_state recv_state;	
	struct swarmkv_msg_hdr receiving_hdr;
	int is_in_conn_table;
	int is_duplicated;
	struct evbuffer* buff_for_sending;
	pthread_mutex_t mutex;
	struct bufferevent *bev;
	evutil_socket_t fd;	
	struct snet_thread *ref_thr;
	UT_hash_handle hh;
};
struct timeout_ctx
{
	timeout_cb *cb;
	void *cb_arg;
	int is_persist;
	struct event *ev;
};
struct snet_reply_ctx
{
	unsigned long long sequence;
	node_t peer;
	struct snet_conn *conn;
	UT_hash_handle hh;
};

struct snet_rpc
{
	unsigned long long sequence;
	node_t peer;
	struct event *timeout_ev;
	on_reply_callback_t *cb;
	void* cb_arg;
	struct snet_thread *thr;
	struct timespec start;
	UT_hash_handle hh;
};

struct snet_stat
{
	long long input_msgs, output_msgs;
	long long input_replies, output_replies;
	long long input_cmds, output_cmds;
	long long input_bytes, output_bytes;
	long long timed_out_rpcs;
	long long unknown_sequence;
	long long input_buffer_sz, output_buffer_sz;
};
struct snet_thread
{
	int thread_id;
	int is_dispatching;
	struct event_base *evbase;
	struct snet_conn *conn_table;
	struct snet_conn *duplicated_conn_table;
	struct snet_conn *designated_conn;//for sending reply
    long long peer_timeout_us;
	struct snet_rpc *rpc_table;
	struct snet_reply_ctx *reply_ctx_table;

	void *ref_logger;
	struct timeout_ctx *to_ctx;
	struct swarmkv_net *ref_net;
	struct swarmkv_module *mod_monitor;
	struct snet_stat stat;
};
struct swarmkv_net
{
	size_t n_thread;
	uuid_t my_uuid;
	unsigned long long sequence_generator;
	int n_connection;
	node_t self;
	unsigned short listen_port;
	struct snet_thread *threads;
	
	struct evconnlistener *listener;	

	void* logger;
	on_cmd_callback_t* on_cmd_cb;
	void* on_cmd_cb_arg;

	pthread_mutex_t stat_lock;
	long long last_input_bytes, last_output_bytes;
	long long last_input_cmds, last_output_cmds;
	struct timespec last_stats;
	double instantaneous_input_kbps, instantaneous_output_kbps;
	double instantaneous_input_cps, instantaneous_output_cps;
};

enum TASK_TYPE
{
	TASK_TYPE_SEND,
	TASK_TYPE_RECEIVE
};

struct snet_task
{
	enum TASK_TYPE task_type;
	enum MSG_TYPE msg_type;
	unsigned long long sequence;
	node_t peer;
	struct event *deferred_ev;	
	struct snet_thread *thr;
	struct snet_conn *conn;
	void *data;
	size_t size;

	on_reply_callback_t *cb;
	void *cb_arg;
};

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
	rpc->thr->stat.timed_out_rpcs++;
	rpc->cb(&rpc->peer, NULL, 0, rpc->cb_arg);
	snet_rpc_free(rpc);
}
int snet_rpc_process_peer_event(struct snet_thread *thr, const node_t *peer)
{
	int n_canceled=0;
	struct snet_rpc *rpc=NULL, *tmp_rpc=NULL;
	HASH_ITER(hh, thr->rpc_table, rpc, tmp_rpc)
	{
		if(0==node_compare(&rpc->peer, peer))
		{
			rpc->cb(&rpc->peer, NULL, 0, rpc->cb_arg);
			snet_rpc_free(rpc);
			n_canceled++;
		}
	}
	return n_canceled;
}
struct snet_rpc *snet_rpc_lookup(struct snet_thread *thr, unsigned long long sequence)
{
	struct snet_rpc *rpc=NULL;
	HASH_FIND(hh, thr->rpc_table, &sequence, sizeof(sequence), rpc);
	return rpc;
}
void snet_rpc_process_reply(struct snet_rpc *rpc, void *data, size_t size)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	swarmkv_monitor_record_peer(rpc->thr->mod_monitor, &rpc->peer, timespec_diff_usec(&rpc->start, &now), rpc->thr->thread_id);
	rpc->cb(&rpc->peer, data, size, rpc->cb_arg);
}
struct snet_rpc * snet_rpc_new(struct snet_thread *thr, node_t *peer, unsigned long long sequence, on_reply_callback_t * cb, void * cb_arg)
{
	struct snet_rpc *rpc=ALLOC(struct snet_rpc, 1);
	rpc->sequence=atomic_inc(&(thr->ref_net->sequence_generator));
	node_copy(&rpc->peer, peer);
	rpc->cb=cb;
	rpc->cb_arg=cb_arg;
	rpc->thr=thr;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &rpc->start);
	struct timeval timeout={thr->peer_timeout_us/(1000*1000), thr->peer_timeout_us%(1000*1000)};
	rpc->timeout_ev=event_new(thr->evbase, -1, 0, snet_rpc_timeout_callback, rpc);
	event_add(rpc->timeout_ev, &timeout);
	HASH_ADD(hh, thr->rpc_table, sequence, sizeof(rpc->sequence), rpc);
	return rpc;
}
static void peer_conn_read_cb(struct bufferevent *bev, void *arg);
void connect_peer_eventcb(struct bufferevent *bev, short events, void *arg);
static void peer_conn_write_cb(struct bufferevent *bev, void *arg);
static void peer_conn_read_cb(struct bufferevent *bev, void *arg);
static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg);
void snet_conn_send(struct snet_conn *conn, const void *hdr, size_t hdr_sz, const void *payload, size_t payload_sz);

struct snet_conn *snet_conn_new_by_connecting(struct snet_thread *thr, const node_t *dest)
{
	struct event_base *base=thr->evbase;
	
	struct snet_conn* conn=ALLOC(struct snet_conn, 1);
	conn->buff_for_sending=evbuffer_new();

	//http://www.wangafu.net/~nickm/libevent-book/Ref6_bufferevent.html
	conn->bev=bufferevent_socket_new(base, -1, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);//BEV_OPT_UNLOCK_CALLBACKS|
	bufferevent_setcb(conn->bev, NULL, NULL, connect_peer_eventcb, conn);
	node_copy(&conn->peer_listen_addr, dest);
	conn->thread_id=thr->thread_id;
	conn->ref_thr=thr;
	node_copy(&conn->connected_from, dest);
	struct sockaddr sa;
	node_to_sockaddr(dest, &sa);
	if (bufferevent_socket_connect(conn->bev, &sa, sizeof(sa)) < 0)
	{
		bufferevent_free(conn->bev);		
		goto error_out;
	}	
	return conn;

error_out:
	evbuffer_free(conn->buff_for_sending);
	conn->buff_for_sending=NULL;
	free(conn);
	return NULL;

}
struct snet_conn *snet_conn_new_by_fd(struct snet_thread *thr, evutil_socket_t fd, struct sockaddr * addr)
{
	struct snet_conn* conn=NULL;
	conn=ALLOC(struct snet_conn, 1);
	conn->buff_for_sending=evbuffer_new();
	int yes=1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
//	evbuffer_enable_locking(conn->buff_for_sending, NULL);
	evutil_make_socket_nonblocking(fd);
	node_init_from_sockaddr(&conn->connected_from, addr);

	conn->bev=bufferevent_socket_new(thr->evbase, fd, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_THREADSAFE);//BEV_OPT_UNLOCK_CALLBACKS|
	//use BEV_OPT_THREADSAFE option for safely access from threads that calling swarmkv_net_send.
	//? use BEV_OPT_UNLOCK_CALLBACKS option because swarmkv_net_send write buffer to output_buffer, and peer_conn_read_cb read input_buffer.
	conn->fd=fd;
	conn->thread_id=thr->thread_id;
	conn->ref_thr=thr;
	bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
	return conn;
}

void snet_task_deferred_run(evutil_socket_t fd, short events, void *arg);


struct snet_task *snet_task_new(struct snet_thread *thr, struct snet_conn *conn, const node_t* peer, enum TASK_TYPE task_type, enum MSG_TYPE msg_type, unsigned long long sequence, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg)
{
	struct snet_task *task=ALLOC(struct snet_task, 1);
	node_copy(&task->peer, peer);
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
	task->conn=conn;
	event_add(task->deferred_ev, NULL);
	event_active(task->deferred_ev, 0, 0);
	return task;
}
void snet_task_free(struct snet_task *task)
{
	free(task->data);
	task->data=NULL;
	event_free(task->deferred_ev);
	task->thr=NULL;
	free(task);
	return;
}

void snet_task_deferred_run(evutil_socket_t fd, short events, void *arg)
{
	struct snet_task *task=(struct snet_task *)arg;
	struct snet_thread *thr=task->thr;

	if(task->task_type==TASK_TYPE_SEND)
	{
		struct snet_conn *conn=NULL;
		if(task->msg_type==MSG_TYPE_CMD)//expecting reply
		{
			struct snet_rpc *rpc=snet_rpc_new(thr, &task->peer, task->sequence, task->cb, task->cb_arg);
//			snet_send_in_evloop(task->thr, &task->peer, rpc->sequence, task->msg_type, task->data, task->size);
			task->sequence=rpc->sequence;
			HASH_FIND(hh, thr->conn_table, &task->peer, sizeof(node_t), conn);
			if(!conn)
			{
				conn=snet_conn_new_by_connecting(thr, &task->peer);
				if(conn)
				{
					HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(node_t), conn);
					conn->is_in_conn_table=1;
				}
				else
				{		
					int err = EVUTIL_SOCKET_ERROR();
					log_fatal(task->thr->ref_logger, MODULE_SWAMRKV_NET, "connect to %s failed, reason %d (%s).",
											task->peer.addr,
											err, evutil_socket_error_to_string(err));
					return;
				}
			}
			thr->stat.output_cmds++;
		}
		else
		{
			struct snet_reply_ctx *reply_ctx=NULL;
			struct snet_reply_ctx key;
			key.sequence=task->sequence;
			node_copy(&key.peer, &task->peer);
			HASH_FIND(hh, thr->reply_ctx_table, &key.sequence, sizeof(key.sequence)+sizeof(key.peer), reply_ctx);
			assert(reply_ctx);
			conn=reply_ctx->conn;
			HASH_DEL(thr->reply_ctx_table, reply_ctx);
			free(reply_ctx);
			thr->stat.output_replies++;
		}
		struct swarmkv_msg_hdr msghdr;
		memset(&msghdr, 0, sizeof(msghdr));
		msghdr.magic=MSG_HDR_MAGIC;
		msghdr.msg_type=task->msg_type;
		msghdr.sequence=task->sequence;
		msghdr.payload_len=task->size;
		node_copy(&msghdr.source, &thr->ref_net->self);
		snet_conn_send(conn, &msghdr, sizeof(msghdr), task->data, task->size);		
		thr->stat.output_msgs++;
	}
	else//receive
	{
		
		assert(task->sequence);
		if(task->msg_type==MSG_TYPE_REPLY)
		{
			struct snet_rpc *rpc=NULL;
			rpc=snet_rpc_lookup(thr, task->sequence);
			if(rpc)
			{
				snet_rpc_process_reply(rpc, task->data, task->size);
				snet_rpc_free(rpc);
			}
			else
			{
				log_fatal(task->thr->ref_logger, MODULE_SWAMRKV_NET, "Unknown reply sequence %llu from %s.",
										task->sequence,
										task->peer.addr);
				thr->stat.unknown_sequence++;
			}
			thr->stat.input_replies++;
		}
		else
		{
			struct snet_reply_ctx *reply_ctx=ALLOC(struct snet_reply_ctx, 1);
			reply_ctx->sequence=task->sequence;
			reply_ctx->conn=task->conn;
			node_copy(&reply_ctx->peer, &task->peer);
			HASH_ADD_KEYPTR(hh, thr->reply_ctx_table, &(reply_ctx->sequence), sizeof(reply_ctx->sequence)+sizeof(reply_ctx->peer), reply_ctx);
			thr->ref_net->on_cmd_cb(&task->peer, task->sequence, task->data, task->size, thr->ref_net->on_cmd_cb_arg);
			thr->stat.input_cmds++;
		}
		thr->stat.input_msgs++;
	}
	snet_task_free(task);
	
	return;
}

static void snet_conn_table_remove(struct snet_conn *conn)
{
	struct snet_thread *thr=conn->ref_thr;
	if(conn->is_in_conn_table)
	{
		HASH_DEL(thr->conn_table, conn);
		conn->is_in_conn_table=0;
	}
	else if(conn->is_duplicated)
	{
		HASH_DEL(thr->duplicated_conn_table, conn);
		conn->is_duplicated=0;
	}
	return;
}
static void snet_conn_buffer_size(struct snet_conn *conn, long long *input_buff_sz, long long *output_buff_sz)
{
	struct evbuffer *tmp=NULL;
	if(conn->bev)
	{
		tmp=bufferevent_get_input(conn->bev);
		*input_buff_sz=evbuffer_get_length(tmp);
		tmp=bufferevent_get_output(conn->bev);
		*output_buff_sz=evbuffer_get_length(tmp);
	}
	else
	{
		*input_buff_sz=0;
		*output_buff_sz=evbuffer_get_length(conn->buff_for_sending);
	}
	return;
}
static void snet_conn_free(struct snet_conn *conn)
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
	assert(!conn->is_in_conn_table && !conn->is_duplicated);
	free(conn);
	return;
}
/*
static void swarmkv_connection_kill(struct snet_conn *conn)
{
	conn->need_to_be_kill=1;
	bufferevent_trigger(conn->bev, EV_READ, BEV_TRIG_IGNORE_WATERMARKS);
	return;
}
*/
static void snet_conn_set_peer(struct snet_conn *conn, node_t *peer)
{
	node_copy(&conn->peer_listen_addr, peer);
	return;
}

static void snet_conn_table_add(struct snet_thread *thr, struct snet_conn *conn)
{
	struct snet_conn *tmp_conn=NULL;
	assert(conn->is_in_conn_table==0);
	HASH_FIND(hh, thr->conn_table, &(conn->peer_listen_addr), sizeof(node_t), tmp_conn);
	if(tmp_conn)
	{
		//Simultaneous open
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET,
			"accept duplicated connection from peer %s, existed connection is %s.",
							conn->connected_from.addr,
							tmp_conn->connected_from.addr);
		conn->is_duplicated=1;
		HASH_ADD(hh, thr->duplicated_conn_table, peer_listen_addr, sizeof(conn->peer_listen_addr), conn);
		//snet_conn_table_remove(tmp_conn);
		//swarmkv_connection_kill(tmp_conn);
	}
	else
	{
		HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(conn->peer_listen_addr), conn);
		conn->is_in_conn_table=1;
		log_info(thr->ref_logger, MODULE_SWAMRKV_NET,
				"new peer connection from %s has been set, which is connected from %s.",
				conn->peer_listen_addr.addr,
				conn->connected_from.addr);
	}
	return;
}

static void peer_conn_write_cb(struct bufferevent *bev, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
	return;
}
void snet_thread_buffer_usage_stat(struct snet_thread *thr)
{
	long long total_input_sz=0, total_output_sz=0;
	long long input_sz=0, output_sz=0;
	struct snet_conn *conn=NULL, *tmp=NULL;
	HASH_ITER(hh, thr->conn_table, conn, tmp)
	{
		snet_conn_buffer_size(conn, &input_sz, &output_sz);
		total_input_sz+=input_sz;
		total_output_sz+=output_sz;
	}
	thr->stat.output_buffer_sz=total_output_sz;
	thr->stat.input_buffer_sz=total_input_sz;
	return;
}
static void peer_conn_read_cb(struct bufferevent *bev, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	struct snet_thread *thr=conn->ref_thr;
	assert(0==evbuffer_get_length(conn->buff_for_sending));
    struct evbuffer *input = bufferevent_get_input(bev);
	assert(bev==conn->bev);
	snet_thread_buffer_usage_stat(thr);
	assert(conn->ref_thr == conn->ref_thr->ref_net->threads+conn->thread_id);
	if(conn->need_to_be_kill)
	{
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connection %s is killed",
									conn->connected_from.addr);
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
			thr->stat.input_bytes+=sizeof(struct swarmkv_msg_hdr);
			hdr=(struct swarmkv_msg_hdr*)recv_buff;
			if(hdr->magic!=MSG_HDR_MAGIC)
			{
				log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "invalid message header magic from %s:%u",
										conn->connected_from.addr);
				if(conn->is_in_conn_table)
				{
					snet_conn_table_remove(conn);
				}
				snet_conn_free(conn);
				break;
			}
			if(!conn->is_in_conn_table && !conn->is_duplicated)
			{
				snet_conn_set_peer(conn, &hdr->source);
				snet_conn_table_add(thr, conn);			
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
			snet_task_new(thr, conn, &(conn->peer_listen_addr), TASK_TYPE_RECEIVE, conn->receiving_hdr.msg_type, conn->receiving_hdr.sequence, recv_buff, conn->receiving_hdr.payload_len, NULL, NULL);
			evbuffer_drain(input, conn->receiving_hdr.payload_len);
			thr->stat.input_bytes+=conn->receiving_hdr.payload_len;
			conn->recv_state=RECEIVING_HDR;
			memset(&conn->receiving_hdr, 0, sizeof(conn->receiving_hdr));
		}
	}
	return;
}
static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	struct snet_thread *thr=conn->ref_thr;
	
	int err = EVUTIL_SOCKET_ERROR();
	int canceled_rpc=snet_rpc_process_peer_event(thr, &conn->peer_listen_addr);
	if (events & BEV_EVENT_ERROR || events & BEV_EVENT_EOF)
	{
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "%s connection error, %d (%s). %d RPCs are canceled.",
								conn->connected_from.addr,
								err, evutil_socket_error_to_string(err),
								canceled_rpc);
		//swarmkv-cli's connection has listen port number > 10000.
	}
	
	snet_conn_table_remove(conn);
	snet_conn_free(conn);
	return;
}
void connect_peer_eventcb(struct bufferevent *bev, short events, void *arg)
{
	struct snet_conn* conn= (struct snet_conn*) arg;
	struct snet_thread* thr=conn->ref_thr;
	
	assert(bev==conn->bev);
	if (events & BEV_EVENT_CONNECTED)
	{
		int yes=1;
		evutil_socket_t fd=bufferevent_getfd(conn->bev);
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
		bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
		bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
		bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "peer %s is connected.",
								conn->peer_listen_addr.addr);
    }
	else if (events & BEV_EVENT_ERROR)
    {
		int err = EVUTIL_SOCKET_ERROR();
		int canceled_rpc=snet_rpc_process_peer_event(thr, &conn->peer_listen_addr);
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connect to %s failed %d (%s). %d RPCs are canceled.",
								conn->peer_listen_addr.addr,
								err, evutil_socket_error_to_string(err),
								canceled_rpc);
		snet_conn_table_remove(conn);
		snet_conn_free(conn);
    }
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
	conn->ref_thr->stat.output_bytes += (hdr_sz+payload_sz);
	return;
}
static int get_net_thread_id(const node_t *node, size_t n_thread)
{
	int thread_id=(int)(XXH32(node, sizeof(node_t), 171))%n_thread;
	return thread_id;
}


static void __dummy_event_handler(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_net *net= (struct swarmkv_net *)arg;
	struct timespec now;
	long long input_bytes=0, output_bytes=0;
	long long input_cmds=0, output_cmds=0;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if(now.tv_sec == net->last_stats.tv_sec)
	{
		//The stat interval is too short.
		return;
	}
	if(0 != pthread_mutex_trylock(&net->stat_lock))
	{
		return;
	}
	for(size_t i=0; i<net->n_thread; i++)
	{
		input_bytes += net->threads[i].stat.input_bytes;
		output_bytes += net->threads[i].stat.output_bytes;
		input_cmds += net->threads[i].stat.input_cmds;
		output_cmds += net->threads[i].stat.output_cmds;
	}
	
	long long diff_sec=MAX(now.tv_sec - net->last_stats.tv_sec, 1);
	net->instantaneous_input_kbps=(input_bytes - net->last_input_bytes)*8/1000/diff_sec;
	net->instantaneous_output_kbps=(output_bytes - net->last_output_bytes)*8/1000/diff_sec;
	net->instantaneous_input_cps=(input_cmds - net->last_input_cmds)/diff_sec;
	net->instantaneous_output_cps=(output_cmds - net->last_output_cmds)/diff_sec;

	clock_gettime(CLOCK_MONOTONIC, &net->last_stats);
	net->last_input_bytes=input_bytes;
	net->last_output_bytes=output_bytes;
	net->last_input_cmds=input_cmds;
	net->last_output_cmds=output_cmds;
	pthread_mutex_unlock(&net->stat_lock);
	return;
}

static __thread int snet_thread_id=-1;
void snet_send(struct swarmkv_net *net, const node_t *peer, enum MSG_TYPE msg_type, const void *data, size_t size, unsigned long long sequence, on_reply_callback_t *cb, void *cb_arg)
{
	struct snet_thread *thr=NULL;
	if(snet_thread_id>=0)//Invoked from swarmkv worker threads.
	{
		thr=net->threads+snet_thread_id;
	}
	else
	{
		thr=net->threads+random()%net->n_thread;
		assert(msg_type!=MSG_TYPE_REPLY);
	}
	snet_task_new(thr, thr->designated_conn, peer, TASK_TYPE_SEND, msg_type, sequence, data, size, cb, cb_arg);
	return;
}
void swarmkv_net_send_cmd(struct swarmkv_net *net, const node_t *peer, const void *data, size_t size, on_reply_callback_t *cb, void *cb_arg)
{
	snet_send(net, peer, MSG_TYPE_CMD, data, size, 0, cb, cb_arg);
}
void swarmkv_net_send_reply(struct swarmkv_net *net, const node_t *peer, const void *data, size_t size, unsigned long long sequence)
{	
	snet_send(net, peer, MSG_TYPE_REPLY, data, size, sequence, NULL, NULL);
}

static void accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
	struct snet_thread *thr=NULL;
	struct snet_conn *conn=NULL;
	node_t peer;
	node_init_from_sockaddr(&peer, addr);
	int thread_id=get_net_thread_id(&peer, net->n_thread);
	thr=net->threads+thread_id;
	conn=snet_conn_new_by_fd(thr, fd, addr);

	log_debug(net->logger, MODULE_SWAMRKV_NET, "accept connection from %s.",
							conn->connected_from.addr);

	return;
}
static void __accept_error_cb(struct evconnlistener *listener, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
    int err = EVUTIL_SOCKET_ERROR();
	log_fatal(net->logger, MODULE_SWAMRKV_NET, "accept peer connection failed %d (%s).",
							err, evutil_socket_error_to_string(err));
}

struct swarmkv_net* swarmkv_net_new(const struct swarmkv_options *opts, node_t* self, void* logger, char **err)
{
	struct swarmkv_net* net=NULL;	
    /* adds locking, only required if accessed from separate threads */
	evthread_use_pthreads();
	net=ALLOC(struct swarmkv_net, 1);
	net->logger=logger;
	net->n_thread=opts->nr_worker_threads;
	net->sequence_generator=1;
	net->threads=ALLOC(struct snet_thread, net->n_thread);
	struct snet_thread *thr=NULL;
	for(size_t i=0; i<net->n_thread; i++)
	{
		thr=net->threads+i;
		thr->evbase=event_base_new();
		thr->thread_id=i;
		thr->peer_timeout_us=opts->cluster_timeout_us;
		thr->ref_logger=logger;
		thr->ref_net=net;
		thr->conn_table=NULL;
		thr->rpc_table=NULL;
	}

	//Refer to http://www.wangafu.net/~nickm/libevent-book/Ref8_listener.html
	struct sockaddr_in sin;
	unsigned int port;
	char addr_str[16];
	node_parse(self, &port, addr_str, sizeof(addr_str));
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);	// Listen on 0.0.0.0
	sin.sin_port = htons((unsigned short)port);
	net->listener=evconnlistener_new_bind(net->threads[0].evbase, accept_peer_connection_cb, net, 
										LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));
	if(!net->listener)
	{
		asprintf(err, "listen on port %u failed", port);
		free(net);
		return NULL;
	}
	evconnlistener_set_error_cb(net->listener, __accept_error_cb);
	int sockfd=evconnlistener_get_fd(net->listener);
    struct sockaddr_storage ss;
	ev_socklen_t socklen = sizeof(ss);
	if (getsockname(sockfd, (struct sockaddr *)&ss, &socklen) < 0)
	{
		asprintf(err, "getsockname failed");
	}
	if(port==0)
	{
		port=ntohs(((struct sockaddr_in*)&ss)->sin_port);
	}
	node_init(self, addr_str, port);
	node_copy(&net->self, self);
	net->listen_port=port;
	pthread_mutex_init(&net->stat_lock, NULL);
	return net;
}
void swarmkv_net_info(struct swarmkv_net *net, struct snet_info *info)
{
	struct snet_thread *thr=NULL;
	memset(info, 0, sizeof(struct snet_info));
	for(size_t i=0; i<net->n_thread; i++)
	{
		thr=net->threads+i;
		info->connections += (HASH_COUNT(thr->conn_table) + HASH_COUNT(thr->duplicated_conn_table));
		info->pending_rpcs += HASH_COUNT(thr->rpc_table);
		info->timed_out_rpcs += thr->stat.timed_out_rpcs;
		info->input_bytes += thr->stat.input_bytes;
		info->input_cmds += thr->stat.input_cmds;
		info->input_replies += thr->stat.input_replies;
		info->input_msgs += thr->stat.input_msgs;

		info->output_bytes += thr->stat.output_bytes;
		info->output_cmds += thr->stat.output_cmds;
		info->output_replies += thr->stat.output_replies;
		info->output_msgs += thr->stat.output_msgs;
		info->input_buffer_sz += thr->stat.input_buffer_sz;
		info->output_buffer_sz += thr->stat.output_buffer_sz;
		info->unknown_sequence += thr->stat.unknown_sequence;
	}
	info->instantaneous_input_kbps=net->instantaneous_input_kbps;
	info->instantaneous_output_kbps=net->instantaneous_output_kbps;
	info->instantaneous_input_cps=net->instantaneous_input_cps;
	info->instantaneous_output_cps=net->instantaneous_output_cps;
	return;
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

void swarmkv_net_timer_add(struct swarmkv_net *net, int thread_id, int usec, int is_persist, timeout_cb *cb, void *cb_arg)
{
	struct timeout_ctx *ctx=ALLOC(struct timeout_ctx, 1);
	ctx->cb=cb;
	ctx->is_persist=is_persist;
	ctx->cb_arg=cb_arg;
	ctx->ev=event_new(net->threads[thread_id].evbase, -1, EV_PERSIST, wrap_timeout_handler, ctx);

	struct timeval timer_delay = {usec/(1000*1000), usec%(1000*1000)};
	evtimer_add(ctx->ev, &timer_delay);
	assert(net->threads[thread_id].to_ctx==NULL);
	net->threads[thread_id].to_ctx=ctx;
}
void swarmkv_net_stop(struct swarmkv_net* net)
{
	struct snet_thread *thr=NULL;

	for(size_t i=0; i<net->n_thread; i++)
	{
		thr=net->threads+i;
		if(thr->is_dispatching)
		{
			event_base_loopexit(thr->evbase, NULL);
			thr->is_dispatching=0;
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
	struct snet_reply_ctx *rctx=NULL, *tmp_rctx=NULL;
	evconnlistener_free(net->listener);
	for(i=0; i<net->n_thread; i++)
	{	
		thr=net->threads+i;
		if(thr->to_ctx)
		{
			timeout_ctx_free(thr->to_ctx);
			thr->to_ctx=NULL;
		}
		HASH_ITER(hh, thr->conn_table, conn, tmp)
		{
			snet_conn_table_remove(conn);
			snet_conn_free(conn);
		}
		HASH_ITER(hh, thr->duplicated_conn_table, conn, tmp)
		{
			snet_conn_table_remove(conn);
			snet_conn_free(conn);
		}		
		HASH_ITER(hh, thr->rpc_table, rpc, tmp_rpc)
		{
			snet_rpc_free(rpc);
		}
		HASH_ITER(hh, thr->reply_ctx_table, rctx, tmp_rctx)
		{
			HASH_DEL(thr->reply_ctx_table, rctx);
			free(rctx);
		}
		event_base_free(thr->evbase);
	}
	pthread_mutex_destroy(&net->stat_lock);
	free(net->threads);

	free(net);
	return;
}

void swarmkv_net_set_on_cmd_cb(struct swarmkv_net* net, on_cmd_callback_t on_cmd_cb, void* arg)
{
	net->on_cmd_cb_arg=arg;
	net->on_cmd_cb=on_cmd_cb;
}
void swarmkv_net_set_monitor_handle(struct swarmkv_net* net, struct swarmkv_module *mod_monitor)
{
	for(size_t i=0; i<net->n_thread; i++)
	{
		net->threads[i].mod_monitor=mod_monitor;
	}
}
void swarmkv_net_dispath(struct swarmkv_net *net, int thread_id)
{
	assert(snet_thread_id<0);
	snet_thread_id=thread_id;
	struct event * ev = event_new(net->threads[thread_id].evbase, -1, EV_PERSIST, __dummy_event_handler, net);
	struct timeval timer_delay = {2, 0};
	net->threads[thread_id].is_dispatching=1;
	evtimer_add(ev, &timer_delay);
	int ret=event_base_dispatch(net->threads[thread_id].evbase);
	if(net->threads[thread_id].is_dispatching){
		log_fatal(net->logger, MODULE_SWAMRKV_NET, "event_base_dispatch() returns %d abnormally", ret);
	}
	event_del(ev);
	event_free(ev);
	return;
}


