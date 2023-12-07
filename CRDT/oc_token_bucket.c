#include "oc_token_bucket.h"
#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>
#define OCTB_REFILL_INTERVAL_MS_DEFAULT	10
struct OC_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_interval_ms;
	struct timeval last_cfg;
};
#define	timeval_to_ms(t)	((t).tv_sec*1000+(t).tv_usec/1000)
struct OC_token_bucket
{
	struct OC_configuration cfg;
	long long refilled;
	long long refill_time_ms;
	//struct timeval refill_timestamp;
	struct PN_counter *consumed;

	/* Local variables */
	int no_reserve;
};
const size_t OCTB_BLOB_HDR_SIZE= offsetof(struct OC_token_bucket, consumed);
struct OC_token_bucket *OC_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	bucket->consumed=PN_counter_new(my_id);
	//memcpy(&bucket->cfg.last_cfg, &now, sizeof(bucket->cfg.last_cfg));
	bucket->cfg.CIR=CIR;
	bucket->cfg.CBS=CBS;
	bucket->cfg.refill_interval_ms=OCTB_REFILL_INTERVAL_MS_DEFAULT;
	bucket->refill_time_ms=timeval_to_ms(now);
	bucket->no_reserve=0;
	bucket->refilled=bucket->cfg.CBS;
	return bucket;
}


void OC_token_bucket_free(struct OC_token_bucket *bucket)
{
	PN_counter_free(bucket->consumed);
	free(bucket);
}
void OC_token_bucket_configure(struct OC_token_bucket *bucket, struct timeval now, long long CIR, long long CBS)
{
	memcpy(&bucket->cfg.last_cfg, &now, sizeof(bucket->cfg.last_cfg));
	if(CIR>=0) bucket->cfg.CIR=CIR;
	if(CBS>=0) bucket->cfg.CBS=CBS;
}
void OC_token_bucket_set_no_reserve(struct OC_token_bucket *bucket)
{
	bucket->no_reserve=1;
}

long long OC_token_bucket_consume(struct OC_token_bucket *bucket, struct timeval now, enum tb_consume_type cmd, long long tokens)
{
	long long delta_time_ms=timeval_to_ms(now)-bucket->refill_time_ms;
	long long consumed=PN_counter_get(bucket->consumed);
	long long refilled=bucket->refilled;
	assert(refilled>=0);
	assert(consumed>=0);
	
	long long new_refilled=0;
	long long available=tb_available(bucket->cfg.CIR, bucket->cfg.CBS, consumed, 
				refilled, delta_time_ms, bucket->cfg.refill_interval_ms, &new_refilled);
	
	int infinite_flag=0;
    if(bucket->cfg.CBS==0 && bucket->cfg.CIR==0)
    {
		infinite_flag=1;
		new_refilled += tokens;
    }
	
	size_t n_replica=PN_counter_replica_num(bucket->consumed);
	if(refilled!=new_refilled)
	{
		bucket->refilled=new_refilled;
		bucket->refill_time_ms=timeval_to_ms(now);
	}
	
	long long allocated=0;
	if(infinite_flag)
	{
		allocated=tokens;
	}
	else
	{
		if(bucket->no_reserve) n_replica=1;
		allocated=tb_consume_reserve_based(bucket->cfg.CIR, available, n_replica, cmd, tokens);
	}
	if(allocated>0)
	{
		PN_counter_incrby(bucket->consumed, allocated);
	}

	assert(allocated>=0);
	return allocated;
}

void OC_token_bucket_info(struct OC_token_bucket *bucket, struct timeval now, struct OC_token_bucket_info *info)
{
	long long delta_time_ms=timeval_to_ms(now)-bucket->refill_time_ms;

	info->CIR=bucket->cfg.CIR;
	info->CBS=bucket->cfg.CBS;
	info->refill_interval_ms=bucket->cfg.refill_interval_ms;
	info->consumed=PN_counter_get(bucket->consumed);
	info->available=tb_available(bucket->cfg.CIR, bucket->cfg.CBS, info->consumed, 
				 bucket->refilled, delta_time_ms, bucket->cfg.refill_interval_ms, &info->refilled);
	info->number_of_replica=PN_counter_replica_num(bucket->consumed);
	return;
}
size_t OC_token_bucket_serialized_size(const struct OC_token_bucket *bucket)
{
	size_t sz=0;
	sz += OCTB_BLOB_HDR_SIZE;
	sz += PN_counter_serialized_size(bucket->consumed);
	return sz;
}
void OC_token_bucket_serialize(const struct OC_token_bucket *bucket, char **blob, size_t *blob_sz)
{
	size_t offset=0;
	size_t sz=OC_token_bucket_serialized_size(bucket);
	char *buffer=ALLOC(char, sz);
	memcpy(buffer, bucket, OCTB_BLOB_HDR_SIZE);
	offset += OCTB_BLOB_HDR_SIZE;

	char *pnc_blob=NULL;
	size_t pnc_blob_sz=0;
	PN_counter_serialize(bucket->consumed, &pnc_blob, &pnc_blob_sz);
	memcpy(buffer+offset, pnc_blob, pnc_blob_sz);
	offset+=pnc_blob_sz;
	free(pnc_blob);
	assert(offset==sz);
	*blob_sz=sz;
	*blob=buffer;
	return;
}
struct OC_token_bucket *OC_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	size_t offset=0;
	memcpy(bucket, blob+offset, OCTB_BLOB_HDR_SIZE);
	offset += OCTB_BLOB_HDR_SIZE;
	bucket->consumed=PN_counter_deserialize(blob+offset, blob_sz-offset);
	offset += PN_counter_serialized_size(bucket->consumed);
	assert(offset <= blob_sz);
	return bucket;
}
void OC_token_bucket_merge(struct OC_token_bucket *dst, const struct OC_token_bucket *src)
{
	if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), <))//Last-Write-Wins
	{
		memcpy(&dst->cfg, &src->cfg, sizeof(src->cfg));
	}
	PN_counter_merge(dst->consumed, src->consumed);
	
	assert(dst->refilled>=0 && src->refilled>=0);
	if(dst->refilled < src->refilled)//Bigger refilled wins
	{
		dst->refilled=src->refilled;
		dst->refill_time_ms=src->refill_time_ms;
		//memcpy(&dst->refill_timestamp, &src->refill_timestamp, sizeof(dst->refill_timestamp));
	}	
	return;
}
void OC_token_bucket_merge_blob(struct OC_token_bucket *bucket, const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *to_merge=OC_token_bucket_deserialize(blob, blob_sz);
	OC_token_bucket_merge(bucket, to_merge);
	OC_token_bucket_free(to_merge);
	return;
}

void OC_token_bucket_change_uuid(struct OC_token_bucket *bucket, uuid_t new_uuid)
{
	struct PN_counter *tmp=bucket->consumed;
	bucket->consumed=PN_counter_new(new_uuid);
	PN_counter_merge(bucket->consumed, tmp);
	PN_counter_free(tmp);
	return;
}
struct OC_token_bucket *OC_token_bucket_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *bucket=OC_token_bucket_deserialize(blob, blob_sz);
	OC_token_bucket_change_uuid(bucket, uuid);
	return bucket;
}
size_t OC_token_bucket_mem_size(const struct OC_token_bucket *bucket)
{
	size_t sz=0;
	sz += sizeof(struct OC_token_bucket);
	sz += PN_counter_mem_size(bucket->consumed);
	return sz;
}