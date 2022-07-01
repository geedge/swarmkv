#include "swarmkv_rpc.h"
#include "swarmkv_utils.h"

#include "uthash.h"
#include <pthread.h>
//rr: request and response
struct swarmkv_rpc
{
	unsigned long long sequence;
	struct timespec create_time;
	swarmkv_rpc_callback_func_t* callback_func;
	void* callback_arg;
	UT_hash_handle hh;
};

struct swarmkv_rpc_mgr
{
	struct swarmkv_rpc *table;
	unsigned long long sequence_generator;
	pthread_mutex_t mutex;
};

struct swarmkv_rpc_mgr *swarmkv_rpc_mgr_new()
{
	struct swarmkv_rpc_mgr *mgr=ALLOC(struct swarmkv_rpc_mgr, 1);
	pthread_mutex_init(&mgr->mutex, NULL);
	mgr->sequence_generator=0;
	return mgr;
}
void swarmkv_rpc_mgr_free(struct swarmkv_rpc_mgr *mgr)
{
	struct swarmkv_rpc *rpc=NULL, *tmp=NULL;
	
	HASH_ITER(hh, mgr->table, rpc, tmp)
	{
		HASH_DELETE(hh, mgr->table, rpc);
		free(rpc);
	}
	free(mgr);
	return;
}
unsigned long long swarmkv_rpc_set_callback(struct swarmkv_rpc_mgr *mgr, swarmkv_rpc_callback_func_t * cb, void * cb_arg)
{
	struct swarmkv_rpc *rr=ALLOC(struct swarmkv_rpc, 1);
	rr->sequence=mgr->sequence_generator++;
	rr->callback_func=cb;
	rr->callback_arg=cb_arg;
	pthread_mutex_lock(&mgr->mutex);
	HASH_ADD(hh, mgr->table, sequence, sizeof(rr->sequence), rr);
	pthread_mutex_unlock(&mgr->mutex);

	return rr->sequence;
}
int swarmkv_rpc_get_callback(struct swarmkv_rpc_mgr *mgr, unsigned long long rpc_sequence, swarmkv_rpc_callback_func_t ** cb, void ** cb_arg)
{
	struct swarmkv_rpc *rr=NULL;
	int ret=0;
	pthread_mutex_lock(&mgr->mutex);
	HASH_FIND(hh, mgr->table, &rpc_sequence, sizeof(rpc_sequence), rr);
	if(!rr)
	{
		ret=0;
	}
	else
	{
		*cb=rr->callback_func;
		*cb_arg=rr->callback_arg;
		HASH_DELETE(hh, mgr->table, rr);
		free(rr);
		ret=1;
	}
	pthread_mutex_unlock(&mgr->mutex);
	return ret;
}



