#include "swarmkv_rr.h"
#include "swarmkv_utils.h"

#include "uthash.h"
#include <pthread.h>
//rr: request and response
struct swarmkv_rr
{
	unsigned long long sequence;
	struct timespec create_time;
	swarmkv_rr_callback_func_t* callback_func;
	void* callback_arg;
	UT_hash_handle hh;
};

struct swarmkv_rr_mgr
{
	struct swarmkv_rr *table;
	unsigned long long sequence_generator;
	pthread_mutex_t mutex;
};

struct swarmkv_rr_mgr *swarmkv_rr_mgr_new()
{
	struct swarmkv_rr_mgr *mgr=ALLOC(struct swarmkv_rr_mgr, 1);
	pthread_mutex_init(&mgr->mutex, NULL);
	mgr->sequence_generator=0;
	return mgr;
}
void swarmkv_rr_mgr_free(struct swarmkv_rr_mgr *mgr)
{
	struct swarmkv_rr *rr=NULL, *tmp_rr=NULL;
	
	HASH_ITER(hh, mgr->table, rr, tmp_rr)
	{
		HASH_DELETE(hh, mgr->table, rr);
		free(rr);
	}
	return;
}
unsigned long long swarmkv_rr_add(struct swarmkv_rr_mgr *mgr, swarmkv_rr_callback_func_t * cb, void * cb_arg)
{
	struct swarmkv_rr *rr=ALLOC(struct swarmkv_rr, 1);
	rr->sequence=mgr->sequence_generator++;
	rr->callback_func=cb;
	rr->callback_arg=cb_arg;
	pthread_mutex_lock(&mgr->mutex);
	HASH_ADD(hh, mgr->table, sequence, sizeof(rr->sequence), rr);
	pthread_mutex_unlock(&mgr->mutex);

	return rr->sequence;
}
int swarmkv_rr_get(struct swarmkv_rr_mgr *mgr, unsigned long long rr_sequence, swarmkv_rr_callback_func_t ** cb, void ** cb_arg)
{
	struct swarmkv_rr *rr=NULL;
	int ret=0;
	pthread_mutex_lock(&mgr->mutex);
	HASH_FIND(hh, mgr->table, &rr_sequence, sizeof(rr_sequence), rr);
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



