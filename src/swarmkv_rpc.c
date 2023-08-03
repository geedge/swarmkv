#include "swarmkv_rpc.h"
#include "swarmkv_utils.h"
#include "uthash.h"

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

struct swarmkv_rpc
{
	unsigned long long sequence;
	struct event *timeout_ev;
	struct future * f;
	struct timespec start;
	int thread_id;
	struct swarmkv_rpc_mgr *ref_mgr;
	UT_hash_handle hh;
};
struct swarmkv_rpc_mgr
{
	long long seq_generator;
	int nr_worker_threads;
	unsigned int timeout_us;
	struct event_base **evbases;//reference to db->evbases[i]
	struct swarmkv_rpc **rpc_table;

	//stats
	long long timed_out_rpcs;
	long long unknown_sequence;
};
void swarmkv_rpc_free(struct swarmkv_rpc *rpc)
{
	event_del(rpc->timeout_ev);
	event_free(rpc->timeout_ev);
	HASH_DELETE(hh, rpc->ref_mgr->rpc_table[rpc->thread_id], rpc);
	free(rpc);
}
struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new(const struct swarmkv_options *opts, struct event_base *evbases[], int nr_threads)
{
	struct swarmkv_rpc_mgr *mgr=ALLOC(struct swarmkv_rpc_mgr, 1);
	mgr->seq_generator=0;
	mgr->nr_worker_threads=nr_threads;
	mgr->evbases=evbases;
	mgr->rpc_table=ALLOC(struct swarmkv_rpc *, nr_threads);
	mgr->timeout_us=opts->cluster_timeout_us;
	return mgr;
}
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr)
{
	struct swarmkv_rpc *rpc=NULL, *tmp=NULL;
	for(int i=0; i<mgr->nr_worker_threads; i++)
	{
		HASH_ITER(hh, mgr->rpc_table[i], rpc, tmp)
		{
			struct promise *p=future_to_promise(rpc->f);
			promise_failed(p, FUTURE_ERROR_CANCEL, NULL);			
			swarmkv_rpc_free(rpc);
		}
	}
	
	free(mgr->rpc_table);
	free(mgr);
}

static void rpc_timeout_callback(evutil_socket_t fd, short events, void *arg)
{	
	struct swarmkv_rpc *rpc=(struct swarmkv_rpc *)arg;
	rpc->ref_mgr->timed_out_rpcs++;
	struct promise *p=future_to_promise(rpc->f);
	promise_failed(p, FUTURE_ERROR_TIMEOUT, "rpc timed out");
	swarmkv_rpc_free(rpc);
}

long long swarmkv_rpc_launch(struct swarmkv_rpc_mgr *mgr, int thread_id, struct future *f)
{
	struct swarmkv_rpc *rpc=ALLOC(struct swarmkv_rpc, 1);
	rpc->sequence=++mgr->seq_generator;
	struct timeval timeout={mgr->timeout_us/(1000*1000), mgr->timeout_us%(1000*1000)};
	rpc->timeout_ev=event_new(mgr->evbases[thread_id], -1, 0, rpc_timeout_callback, rpc);
	event_add(rpc->timeout_ev, &timeout);
	rpc->thread_id=thread_id;
	rpc->ref_mgr=mgr;
	rpc->f=f;
	HASH_ADD(hh, mgr->rpc_table[thread_id], sequence, sizeof(rpc->sequence), rpc);
	return rpc->sequence;
}
void swarmkv_rpc_complete(struct swarmkv_rpc_mgr *mgr, int thread_id, long long sequence, void *reply)
{
	struct swarmkv_rpc *rpc=NULL;
	HASH_FIND(hh, mgr->rpc_table[thread_id], &sequence, sizeof(sequence), rpc);
	if(rpc==NULL)
	{
		mgr->unknown_sequence++;
		return;
	}
	struct promise *p=future_to_promise(rpc->f);
	promise_success(p, reply);
	swarmkv_rpc_free(rpc);
}