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
struct swarmkv_rpc_thr
{
	struct swarmkv_rpc *rpc_table;
	struct event_base *evbase; //reference to db->evbases[i]
	const struct timeval *common_timeout;
};
struct swarmkv_rpc_mgr
{
	long long seq_generator;
	int nr_worker_threads;
	unsigned int timeout_us;
	struct swarmkv_rpc_thr *threads;

	//stats
	long long timed_out_rpcs;
	long long unknown_sequence;
};
void swarmkv_rpc_free(struct swarmkv_rpc *rpc)
{
	event_del(rpc->timeout_ev);
	event_free(rpc->timeout_ev);
	struct swarmkv_rpc_thr *thr=rpc->ref_mgr->threads+rpc->thread_id;
	HASH_DELETE(hh, thr->rpc_table, rpc);
	free(rpc);
}
struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new(const struct swarmkv_options *opts, struct event_base *evbases[], int nr_threads)
{
	struct swarmkv_rpc_mgr *mgr=ALLOC(struct swarmkv_rpc_mgr, 1);
	mgr->seq_generator=0;
	mgr->nr_worker_threads=nr_threads;
	mgr->timeout_us=opts->cluster_timeout_us;
	mgr->threads=ALLOC(struct swarmkv_rpc_thr, nr_threads);
	
	struct timeval duration={mgr->timeout_us/(1000*1000), mgr->timeout_us%(1000*1000)};
	struct swarmkv_rpc_thr *thr=NULL;
	for(int i=0; i<nr_threads; i++)
	{
		thr=mgr->threads+i;
		thr->evbase=evbases[i];
		thr->rpc_table=NULL;
		thr->common_timeout=event_base_init_common_timeout(evbases[i], &duration);
	}
	return mgr;
}
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr)
{
	struct swarmkv_rpc *rpc=NULL, *tmp=NULL;
	struct swarmkv_rpc_thr *thr=NULL;
	for(int i=0; i<mgr->nr_worker_threads; i++)
	{
		thr=mgr->threads+i;
		HASH_ITER(hh, thr->rpc_table, rpc, tmp)
		{
			struct promise *p=future_to_promise(rpc->f);
			promise_failed(p, FUTURE_ERROR_CANCEL, NULL);			
			swarmkv_rpc_free(rpc);
		}
	}
	
	free(mgr->threads);
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
	struct swarmkv_rpc_thr *thr=mgr->threads+thread_id;
	struct swarmkv_rpc *rpc=ALLOC(struct swarmkv_rpc, 1);
	rpc->sequence=__atomic_add_fetch(&mgr->seq_generator, 1, __ATOMIC_SEQ_CST);
	//struct timeval timeout={mgr->timeout_us/(1000*1000), mgr->timeout_us%(1000*1000)};
	rpc->timeout_ev=event_new(thr->evbase, -1, 0, rpc_timeout_callback, rpc);
	event_priority_set(rpc->timeout_ev, 1);
	event_add(rpc->timeout_ev, thr->common_timeout);
	rpc->thread_id=thread_id;
	rpc->ref_mgr=mgr;
	rpc->f=f;
	HASH_ADD(hh, thr->rpc_table, sequence, sizeof(rpc->sequence), rpc);
	return rpc->sequence;
}
void swarmkv_rpc_complete(struct swarmkv_rpc_mgr *mgr, int thread_id, long long sequence, void *reply)
{
	struct swarmkv_rpc *rpc=NULL;
	HASH_FIND(hh, mgr->threads[thread_id].rpc_table, &sequence, sizeof(sequence), rpc);
	if(rpc==NULL)
	{
		mgr->unknown_sequence++;
		return;
	}
	struct promise *p=future_to_promise(rpc->f);
	promise_success(p, reply);
	swarmkv_rpc_free(rpc);
}
size_t swarmkv_rpc_mgr_count(struct swarmkv_rpc_mgr *mgr, int thread_id)
{
	assert(thread_id>=0 && thread_id<mgr->nr_worker_threads);
	return HASH_COUNT(mgr->threads[thread_id].rpc_table);
}