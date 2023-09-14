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
	const struct swarmkv_msg *receiving_hdr;//pointer of receiving buffer
	int is_in_conn_table;
	int is_duplicated;
	struct evbuffer* buff_for_sending;
	pthread_mutex_t mutex;
	struct bufferevent *bev;
	evutil_socket_t fd;	
	struct snet_thread *ref_thr;
	UT_hash_handle hh;
};
struct snet_stat
{
	long long input_msgs, output_msgs;
	long long input_bytes, output_bytes;
	long long input_buffer_sz, output_buffer_sz;
	long long output_drop_msgs;
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

	void *ref_logger;
	struct swarmkv_net *ref_net;
	struct swarmkv_module *mod_monitor;
	struct snet_stat stat;
};
struct swarmkv_net
{
	size_t nr_threads;
	uuid_t my_uuid;
	int n_connection;
	node_t self;
	struct snet_thread *threads;
	
	struct evconnlistener *listener;	

	void* logger;
	on_msg_callback_t *on_msg_cb;
	void *on_msg_cb_arg;
	struct event * stat_ev;
	long long last_input_bytes, last_output_bytes;
	long long last_input_msgs, last_output_msgs;
	struct timespec last_stats;
	double instantaneous_input_kbps, instantaneous_output_kbps;
	double instantaneous_input_msgs, instantaneous_output_msgs;
};

static void peer_conn_read_cb(struct bufferevent *bev, void *arg);
void connect_peer_eventcb(struct bufferevent *bev, short events, void *arg);
static void peer_conn_write_cb(struct bufferevent *bev, void *arg);
static void peer_conn_read_cb(struct bufferevent *bev, void *arg);
static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg);

struct snet_conn *snet_conn_new_by_connecting(struct snet_thread *thr, const node_t *dest)
{
	struct event_base *base=thr->evbase;
	
	struct snet_conn* conn=ALLOC(struct snet_conn, 1);
	conn->buff_for_sending=evbuffer_new();
	conn->fd=-1;
	
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
	evutil_make_socket_closeonexec(fd);
	node_init_from_sockaddr(&conn->connected_from, addr);

	conn->bev=bufferevent_socket_new(thr->evbase, fd, BEV_OPT_DEFER_CALLBACKS|BEV_OPT_UNLOCK_CALLBACKS);//BEV_OPT_UNLOCK_CALLBACKS|BEV_OPT_THREADSAFE
	//use BEV_OPT_THREADSAFE option for safely access from threads that calling swarmkv_net_send.
	//? use BEV_OPT_UNLOCK_CALLBACKS option because swarmkv_net_send write buffer to output_buffer, and peer_conn_read_cb read input_buffer.
	conn->fd=fd;
	conn->thread_id=thr->thread_id;
	conn->ref_thr=thr;
	bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
	bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
	return conn;
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
	struct swarmkv_msg *msg=NULL;
	if(conn->need_to_be_kill)
	{
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connection %s is killed",
									conn->connected_from.addr);
		assert(conn->is_in_conn_table==0);		
		snet_conn_free(conn);
		return;
	}
	const char *recv_buff=NULL;
	struct swarmkv_msg* hdr=NULL;
	while(1)
	{
		if(conn->recv_state==RECEIVING_HDR)
		{
			recv_buff=(const char*) evbuffer_pullup(input, SWARMKV_MSG_HDR_SIZE);
			if(!recv_buff)
			{
				bufferevent_setwatermark(bev, EV_READ, SWARMKV_MSG_HDR_SIZE, 0);
				conn->recv_state=RECEIVING_HDR;
				break;
			}
			hdr = (struct swarmkv_msg*) recv_buff;
			if(hdr->magic != SWARMKV_MSG_MAGIC)
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
				snet_conn_set_peer(conn, &hdr->caller);
				snet_conn_table_add(thr, conn);			
			}
			conn->recv_state=RECEIVING_PAYLOAD;
			conn->receiving_hdr=(struct swarmkv_msg *)recv_buff;
		}
		if(conn->recv_state==RECEIVING_PAYLOAD)
		{
			size_t msg_sz=SWARMKV_MSG_HDR_SIZE+conn->receiving_hdr->payload_len;
			recv_buff=(const char*)evbuffer_pullup(input, msg_sz);
			if(!recv_buff)//not enough buffer
			{
				bufferevent_setwatermark(conn->bev, EV_READ, msg_sz, 0);
				conn->recv_state=RECEIVING_PAYLOAD;
				break;
			}
			msg=swarmkv_msg_deserialize(recv_buff, msg_sz);
			//onwership of msg is transfered to on_msg_cb
			thr->ref_net->on_msg_cb(msg, thr->ref_net->on_msg_cb_arg);
			//swarmkv_msg_free(msg);
			msg=NULL;
			evbuffer_drain(input, msg_sz);
			thr->stat.input_msgs++;
			thr->stat.input_bytes += msg_sz;
			conn->recv_state=RECEIVING_HDR;
		}
	}
	return;
}
static void peer_conn_event_cb(struct bufferevent *bev, short events, void *arg)
{
	struct snet_conn *conn=(struct snet_conn*)arg;
	struct snet_thread *thr=conn->ref_thr;
	
	int err = EVUTIL_SOCKET_ERROR();
	if (events & BEV_EVENT_ERROR || events & BEV_EVENT_EOF)
	{
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "%s connection error, %d (%s).",
								conn->connected_from.addr,
								err, evutil_socket_error_to_string(err));
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
		conn->fd=fd;
		evutil_make_socket_closeonexec(fd);
		bufferevent_write_buffer(conn->bev, conn->buff_for_sending);
		bufferevent_setcb(conn->bev, peer_conn_read_cb, peer_conn_write_cb, peer_conn_event_cb, conn);
		bufferevent_enable(conn->bev, EV_READ | EV_WRITE);
		log_debug(thr->ref_logger, MODULE_SWAMRKV_NET, "peer %s is connected.",
								conn->peer_listen_addr.addr);
    }
	else if (events & BEV_EVENT_ERROR)
    {
		int err = EVUTIL_SOCKET_ERROR();
		log_fatal(thr->ref_logger, MODULE_SWAMRKV_NET, "connect to %s failed %d (%s).",
								conn->peer_listen_addr.addr,
								err, evutil_socket_error_to_string(err));
        if(conn->fd<=0)
        {
            evutil_socket_t fd=bufferevent_getfd(conn->bev);
            conn->fd=fd;
        }
		snet_conn_table_remove(conn);
		snet_conn_free(conn);
    }
}

static int get_net_thread_id(const node_t *node, size_t n_thread)
{
	int thread_id=(int)(XXH32(node, sizeof(node_t), 171))%n_thread;
	return thread_id;
}


static void snet_stat_periodic(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_net *net= (struct swarmkv_net *)arg;
	struct timespec now;
	long long input_bytes=0, output_bytes=0;
	long long input_msgs=0, output_msgs=0;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if(now.tv_sec == net->last_stats.tv_sec)
	{
		//The stat interval is too short.
		return;
	}

	for(size_t i=0; i<net->nr_threads; i++)
	{
		input_bytes += net->threads[i].stat.input_bytes;
		output_bytes += net->threads[i].stat.output_bytes;
		input_msgs += net->threads[i].stat.input_msgs;
		output_msgs += net->threads[i].stat.output_msgs;
	}
	
	long long diff_sec=MAX(now.tv_sec - net->last_stats.tv_sec, 1);
	net->instantaneous_input_kbps=(input_bytes - net->last_input_bytes)*8/1000/diff_sec;
	net->instantaneous_output_kbps=(output_bytes - net->last_output_bytes)*8/1000/diff_sec;
	net->instantaneous_input_msgs=(input_msgs - net->last_input_msgs)/diff_sec;
	net->instantaneous_output_msgs=(output_msgs - net->last_output_msgs)/diff_sec;

	clock_gettime(CLOCK_MONOTONIC, &net->last_stats);
	net->last_input_bytes=input_bytes;
	net->last_output_bytes=output_bytes;
	net->last_input_msgs=input_msgs;
	net->last_output_msgs=output_msgs;

	return;
}
static void accept_peer_connection_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr * addr, int socklen, void *arg)
{
	struct swarmkv_net *net = (struct swarmkv_net*) arg;
	struct snet_thread *thr=NULL;
	struct snet_conn *conn=NULL;
	node_t peer;
	node_init_from_sockaddr(&peer, addr);
	int thread_id=get_net_thread_id(&peer, net->nr_threads);
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

struct swarmkv_net *swarmkv_net_new(struct event_base *evbases[], int nr_threads, struct swarmkv_options *opts, struct log_handle *logger, char **err)
{
	struct swarmkv_net *net=NULL;	
	net=ALLOC(struct swarmkv_net, 1);
	net->logger=logger;
	net->nr_threads=nr_threads;
	net->threads=ALLOC(struct snet_thread, net->nr_threads);
	struct snet_thread *thr=NULL;
	
	for(size_t i=0; i<net->nr_threads; i++)
	{
		thr=net->threads+i;
		thr->evbase=evbases[i];
		thr->thread_id=i;
		thr->peer_timeout_us=opts->cluster_timeout_us;
		thr->ref_logger=logger;
		thr->ref_net=net;
		thr->conn_table=NULL;
	}
	net->stat_ev = event_new(evbases[0], -1, EV_PERSIST, snet_stat_periodic, net);
	struct timeval timer_delay = {2, 0};
	evtimer_add(net->stat_ev, &timer_delay);
	
	//Refer to http://www.wangafu.net/~nickm/libevent-book/Ref8_listener.html
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	inet_pton(AF_INET, opts->bind_address, &(sin.sin_addr));
	sin.sin_port = htons((unsigned short)opts->cluster_port);
	net->listener=evconnlistener_new_bind(net->threads[0].evbase, accept_peer_connection_cb, net, 
										LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));
	if(!net->listener)
	{
		asprintf(err, "listen on port %u failed", opts->cluster_port);
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

	opts->cluster_port=ntohs(((struct sockaddr_in*)&ss)->sin_port);
	if(opts->cluster_announce_port==0)
	{
		opts->cluster_announce_port=opts->cluster_port;
	}
	node_init(&net->self, opts->cluster_announce_ip, opts->cluster_announce_port);

	return net;
}
void swarmkv_net_info(struct swarmkv_net *net, struct snet_info *info)
{
	struct snet_thread *thr=NULL;
	memset(info, 0, sizeof(struct snet_info));
	for(size_t i=0; i<net->nr_threads; i++)
	{
		thr=net->threads+i;
		info->connections += (HASH_COUNT(thr->conn_table) + HASH_COUNT(thr->duplicated_conn_table));

		info->input_bytes += thr->stat.input_bytes;
		info->input_msgs += thr->stat.input_msgs;

		info->output_bytes += thr->stat.output_bytes;
		info->output_msgs += thr->stat.output_msgs;
		info->input_buffer_sz += thr->stat.input_buffer_sz;
		info->output_buffer_sz += thr->stat.output_buffer_sz;
		info->output_drop_msgs += thr->stat.output_drop_msgs;
	}
	info->instantaneous_input_kbps=net->instantaneous_input_kbps;
	info->instantaneous_output_kbps=net->instantaneous_output_kbps;
	info->instantaneous_input_msgs=net->instantaneous_input_msgs;
	info->instantaneous_output_msgs=net->instantaneous_output_msgs;
	return;
}

void swarmkv_net_free(struct swarmkv_net *net)
{
	int i=0;
	struct snet_thread *thr=NULL;
	struct snet_conn *conn=NULL, *tmp=NULL;

	evconnlistener_free(net->listener);
	for(i=0; i<net->nr_threads; i++)
	{	
		thr=net->threads+i;

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
		thr->evbase=NULL;
	}
	event_del(net->stat_ev);
	event_free(net->stat_ev);
	free(net->threads);

	free(net);
	return;
}
void swarmkv_net_set_monitor_handle(struct swarmkv_net *net, struct swarmkv_module *mod_monitor)
{
	for(size_t i=0; i<net->nr_threads; i++)
	{
		net->threads[i].mod_monitor=mod_monitor;
	}
}
void swarmkv_net_set_on_msg_callback(struct swarmkv_net *net, on_msg_callback_t *cb, void *cb_arg)
{
	net->on_msg_cb=cb;
	net->on_msg_cb_arg=cb_arg;
	return;
}
#define	MAX_OUTPUT_BUFFER_SIZE	1024*1024*64
int swarmkv_net_send(struct swarmkv_net *net, const node_t *dest, struct swarmkv_msg *msg, const char **err_str)
{
	int tid=__gettid((struct swarmkv *)(net->on_msg_cb_arg));
	assert(tid<net->nr_threads);
	struct snet_thread *thr=net->threads+tid;
	struct snet_conn *conn=NULL;
	HASH_FIND(hh, thr->conn_table, dest, sizeof(node_t), conn);
	if(!conn)
	{
		conn=snet_conn_new_by_connecting(thr, dest);
		if(conn)
		{
			HASH_ADD(hh, thr->conn_table, peer_listen_addr, sizeof(node_t), conn);
			conn->is_in_conn_table=1;
		}
		else
		{		
			int err = EVUTIL_SOCKET_ERROR();
			log_fatal(net->logger, MODULE_SWAMRKV_NET, "connect to %s failed, reason %d (%s).",
									dest->addr,
									err, evutil_socket_error_to_string(err));
			*err_str=evutil_socket_error_to_string(err);
			return -1;
		}
	}
	char *data=NULL;
	size_t size=0;
	swarmkv_msg_serialize(msg,  &data, &size);
	swarmkv_msg_free(msg);
	struct evbuffer* output_buffer=NULL;
	output_buffer=bufferevent_get_output(conn->bev);
	if(!output_buffer) output_buffer=conn->buff_for_sending;
	int ret=0;
	if(evbuffer_get_length(output_buffer) + size > MAX_OUTPUT_BUFFER_SIZE)
	{
		thr->stat.output_drop_msgs++;
		*err_str="output buffer is full";
		ret=-1;
	}
	else
	{
		evbuffer_add(output_buffer, data, size);
		thr->stat.output_bytes += size;
		thr->stat.output_msgs ++;
	}
	free(data);
	return ret;
}