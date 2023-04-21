#include "oc_token_bucket.h"
#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>

struct OC_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_duration_ms;
	struct timeval write_timestamp;
};
struct OC_token_bucket
{
	struct OC_configuration cfg;
	long long refilled;
	struct timeval refill_timestamp;
	struct PN_counter *consumed;
};
struct OC_token_bucket *OC_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	bucket->consumed=PN_counter_new(my_id);
	gettimeofday(&bucket->cfg.write_timestamp, NULL);
	memcpy(&bucket->cfg.write_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	bucket->cfg.CIR=CIR;
	bucket->cfg.CBS=CBS;
	bucket->cfg.refill_duration_ms=10;
	memcpy(&bucket->refill_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	bucket->refilled=bucket->cfg.CBS;
	return bucket;
}


void OC_token_bucket_free(struct OC_token_bucket *bucket)
{
	PN_counter_free(bucket->consumed);
	free(bucket);
}
void OC_token_bucket_configure(struct OC_token_bucket *bucket, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms)
{
	memcpy(&bucket->cfg.write_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	if(CIR>=0) bucket->cfg.CIR=CIR;
	if(CBS>=0) bucket->cfg.CBS=CBS;
	if(refill_duration_ms>0) bucket->cfg.refill_duration_ms=refill_duration_ms;
}


long long OC_token_bucket_control(struct OC_token_bucket *bucket, struct timeval now, enum token_bucket_command cmd, long long tokens)
{
	long long delta_time_ms=timeval_delta_ms(bucket->refill_timestamp, now);
	long long consumed=PN_counter_get(bucket->consumed);
	long long to_add=0;
	long long refilled=bucket->refilled;
	assert(refilled>=0);
	assert(consumed>=0);
	int refill_flag=0;
	int infinite_flag=0;
    if(bucket->cfg.CBS==0 && bucket->cfg.CIR==0)
    {
		infinite_flag=1;
		refilled+=tokens;
		refill_flag=1;
    }

    to_add=bucket->cfg.CIR*delta_time_ms/1000;
	if(delta_time_ms>=bucket->cfg.refill_duration_ms && to_add>0)
	{
		if(refilled<consumed)
		{
			refilled=consumed;
		}
		if(to_add + refilled - consumed < bucket->cfg.CBS)
		{
			refilled+=to_add;
		}
		else
		{
			refilled+=bucket->cfg.CBS - (refilled - consumed);
		}
		refill_flag=1;
	}
	size_t n_replica=PN_counter_replica_num(bucket->consumed);

	
	long long global_available=MAX(refilled-consumed, 0);
	long long reserved=bucket->cfg.CIR*(n_replica-1)/n_replica;
	long long local_available=MAX(global_available-reserved, 0);
//	long long local_available=global_available;
	long long assigned=0;
	switch(cmd)
	{
		case TB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE:
			assigned=infinite_flag?tokens:global_available;
			break;
		case TB_CMD_CONSUME_FORCE:
			assigned=tokens;
			break;
		case TB_CMD_CONSUME_FLEXIBLE:
			assigned=infinite_flag?tokens:MIN(tokens, local_available);;
			break;
		case TB_CMD_CONSUME_NORMAL:
			if(infinite_flag)
			{
				assigned=tokens;
			}
			else
			{
				assigned=(tokens<=local_available) ? tokens:0;
			}
			break;
		case TB_CMD_READ_AVAILABLE:
			return global_available;
		case TB_CMD_READ_REFILLED:
			return refilled;
		case TB_CMD_READ_CONSUEMD:
			return consumed;
		default:
			assert(0);
			break;
	}
	if(refill_flag)
	{
		bucket->refilled=refilled;		
		memcpy(&bucket->refill_timestamp, &now, sizeof(bucket->refill_timestamp));
	}
	assert(assigned>=0);
	PN_counter_incrby(bucket->consumed, assigned);
	return assigned;
}
long long OC_token_bucket_read_refilled(struct OC_token_bucket *bucket)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return OC_token_bucket_control(bucket, now, TB_CMD_READ_REFILLED, 0);

}
long long OC_token_bucket_read_available(struct OC_token_bucket *bucket)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return OC_token_bucket_control(bucket, now, TB_CMD_READ_AVAILABLE, 0);
}
void OC_token_bucket_info(struct OC_token_bucket *bucket, struct OC_token_bucket_info *info)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	info->CIR=bucket->cfg.CIR;
	info->CBS=bucket->cfg.CBS;
	info->refill_duration_ms=bucket->cfg.refill_duration_ms;
	info->consumed=OC_token_bucket_control(bucket, now, TB_CMD_READ_CONSUEMD, 0);
	info->refilled=OC_token_bucket_control(bucket, now, TB_CMD_READ_REFILLED, 0);
	info->available=OC_token_bucket_control(bucket, now, TB_CMD_READ_AVAILABLE, 0);
	return;
}

void OC_token_bucket_serialize(const struct OC_token_bucket *bucket, char **blob, size_t *blob_sz)
{
	char *pnc_blob=NULL;
	size_t pnc_blob_sz=0;
	PN_counter_serialize(bucket->consumed, &pnc_blob, &pnc_blob_sz);

	size_t offset=0;
	size_t mpack_sz=sizeof(struct OC_token_bucket)-sizeof(struct PN_counter*)+pnc_blob_sz;
	char *mpack_buff=(char*)malloc(mpack_sz);
	memcpy(mpack_buff, bucket, sizeof(struct OC_token_bucket)-sizeof(struct PN_counter*));
	offset+=sizeof(struct OC_token_bucket)-sizeof(struct PN_counter*);
	memcpy(mpack_buff+offset, pnc_blob, pnc_blob_sz);

	*blob_sz=mpack_sz;
	*blob=mpack_buff;

	free(pnc_blob);
	pnc_blob=NULL;
	return;
}
struct OC_token_bucket *OC_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	size_t offset=0;
	memcpy(bucket, blob, sizeof(struct OC_token_bucket)-sizeof(struct PN_counter*));
	offset+=sizeof(struct OC_token_bucket)-sizeof(struct PN_counter*);
	bucket->consumed=PN_counter_deserialize(blob+offset, blob_sz-offset);
	return bucket;
}
void OC_token_bucket_merge(struct OC_token_bucket *dst, const struct OC_token_bucket *src)
{
	if(timercmp(&(dst->cfg.write_timestamp), &(src->cfg.write_timestamp), <))//Last-Write-Wins
	{
		memcpy(&dst->cfg, &src->cfg, sizeof(src->cfg));
	}
	PN_counter_merge(dst->consumed, src->consumed);
	
	assert(dst->refilled>=0 && src->refilled>=0);
	if(dst->refilled < src->refilled)//Bigger refilled wins
	{
		dst->refilled=src->refilled;
		memcpy(&dst->refill_timestamp, &src->refill_timestamp, sizeof(dst->refill_timestamp));
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
size_t OC_token_bucket_size(const struct OC_token_bucket *bucket)
{
	size_t sz=0;
	sz=sizeof(struct OC_token_bucket);
	sz+=PN_counter_size(bucket->consumed);
	return sz;
}
