#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>
#include <sys/time.h>

#define REPLICA_ID_SIZE	16

struct counter_item
{
	uuid_t replica_id;
	long long P;
	long long N;
	UT_hash_handle hh;
};
struct counter_item *counter_item_new(uuid_t replica_id)
{
	struct counter_item *item=ALLOC(struct counter_item, 1);
	uuid_copy(item->replica_id, replica_id);
	return item;
}
void counter_item_free(struct counter_item *item)
{
	free(item);
}
struct PN_counter
{

	uuid_t my_id;
	struct timeval write_timestamp;
	long long W; //value wrote by PN_counter_set
	long long _P, _N; //observed value at PN_counter_set;
	struct counter_item *hash_item;
};
struct PN_counter *PN_counter_new(uuid_t my_id)
{
	struct PN_counter *pnc=ALLOC(struct PN_counter, 1);
	uuid_copy(pnc->my_id, my_id);
	return pnc;
}
void PN_counter_free(struct PN_counter *pnc)
{
	struct counter_item *item=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		HASH_DELETE(hh, pnc->hash_item, item);
		counter_item_free(item);
	}
	free(pnc);
}

long long PN_counter_get(const struct PN_counter *pnc)
{
	long long abs_positive_counter=0, abs_negtive_counter=0;
	struct counter_item *item=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		abs_positive_counter+=item->P;
		abs_negtive_counter+=item->N;
	}
	long long value= pnc->W + (abs_positive_counter - pnc->_P) - (abs_negtive_counter - pnc->_N);
	return value;
}
long long PN_counter_set(struct PN_counter *pnc, long long value)
{
	struct counter_item *item=NULL, *tmp=NULL;
	gettimeofday(&pnc->write_timestamp, NULL);
	pnc->W=value;
	pnc->_P=0;
	pnc->_N=0;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		pnc->_P+=item->P;
		pnc->_N+=item->N;
	}
	return PN_counter_get(pnc);
}
long long PN_counter_incrby(struct PN_counter *pnc, long long increment)
{
	struct counter_item *c=NULL;
	HASH_FIND(hh, pnc->hash_item, pnc->my_id, sizeof(pnc->my_id), c);
	if(!c)
	{
		c=counter_item_new(pnc->my_id);
		HASH_ADD_KEYPTR(hh, pnc->hash_item, c->replica_id, sizeof(c->replica_id), c);
	}
	if(increment>0)
	{
		c->P += increment;
	}
	else
	{
		c->N -= increment;
	}
	return PN_counter_get(pnc);
}

void PN_counter_serialize(const struct PN_counter *pnc, char **blob, size_t *blob_sz)
{
	struct counter_item *c=NULL, *tmp=NULL;
	size_t mpack_sz=sizeof(struct PN_counter)-sizeof(pnc->hash_item);
	mpack_sz+=HASH_COUNT(pnc->hash_item)*(sizeof(struct counter_item)-sizeof(UT_hash_handle));
	char *mpack_buff=(char*)malloc(mpack_sz);
	size_t offset=0;
	memcpy(mpack_buff, pnc, sizeof(struct PN_counter)-sizeof(pnc->hash_item));
	offset+=sizeof(struct PN_counter)-sizeof(pnc->hash_item);
	HASH_ITER(hh, pnc->hash_item, c, tmp)
	{
		memcpy(mpack_buff+offset, c, sizeof(struct counter_item)-sizeof(UT_hash_handle));
		offset+=sizeof(struct counter_item)-sizeof(UT_hash_handle);
	}
	assert(offset==mpack_sz);
	*blob_sz=mpack_sz;
	*blob=mpack_buff;
	return;
}
struct PN_counter *PN_counter_deserialize(const char *blob, size_t blob_sz)
{
	struct PN_counter *pnc=ALLOC(struct PN_counter, 1);
	struct counter_item *c=NULL, *tmp=NULL;
	size_t offset=0;
	memcpy(pnc, blob, sizeof(struct PN_counter)-sizeof(pnc->hash_item));
	offset+=sizeof(struct PN_counter)-sizeof(pnc->hash_item);
	while(offset<blob_sz)
	{
		tmp=(struct counter_item *)(blob+offset);
		c=ALLOC(struct counter_item, 1);
		memcpy(c, tmp, sizeof(struct counter_item)-sizeof(UT_hash_handle));
		HASH_ADD_KEYPTR(hh, pnc->hash_item, c->replica_id, sizeof(c->replica_id), c);
		offset+=sizeof(struct counter_item)-sizeof(UT_hash_handle);
	}
	return pnc;
}
void PN_counter_merge(struct PN_counter *dst, const struct PN_counter *src)
{
	struct counter_item *src_item=NULL, *dst_item=NULL, *tmp=NULL;	
	if(timercmp(&dst->write_timestamp, &src->write_timestamp, <))//Last-Write-Wins
	{
		dst->W=src->W;
		dst->_P=src->_P;
		dst->_N=src->_N;
		memcpy(&dst->write_timestamp, &src->write_timestamp, sizeof(dst->write_timestamp));
	}
	HASH_ITER(hh, src->hash_item, src_item, tmp)
	{
		HASH_FIND(hh, dst->hash_item, src_item->replica_id, sizeof(src_item->replica_id), dst_item);
		if(!dst_item)
		{
			dst_item=counter_item_new(src_item->replica_id);
			HASH_ADD_KEYPTR(hh, dst->hash_item, dst_item->replica_id, sizeof(dst_item->replica_id), dst_item);
		}
		dst_item->P=MAX(dst_item->P, src_item->P);
		dst_item->N=MAX(dst_item->N, src_item->N);
	}
	return;
}
void PN_counter_merge_blob(struct PN_counter *pnc, const char *blob, size_t blob_sz)
{
	struct PN_counter *to_merge=PN_counter_deserialize(blob, blob_sz);
	PN_counter_merge(pnc, to_merge);
	PN_counter_free(to_merge);
	return;
}
size_t PN_counter_replica_num(const struct PN_counter *pnc)
{
	return MAX(HASH_COUNT(pnc->hash_item), 1);
}
size_t PN_counter_size(const struct PN_counter *pnc)
{
	size_t sz=0;
	sz+=HASH_COUNT(pnc->hash_item) * sizeof(struct counter_item);
	sz+=sizeof(struct PN_counter);
	return sz;
}