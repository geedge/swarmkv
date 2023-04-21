#include "bulk_token_bucket.h"
#include "st_hyperloglog.h"
#include "g_array.h"
#include "crdt_utils.h"

#include "xxhash.h"
#include "uthash.h"
#include "mpack.h"

#include <math.h>
#include <assert.h>

#define PERTURB_INTERVAL_MAX_MS  8000


struct btb_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_interval_ms;
    long long bucket_num;
	struct timeval timestamp;
};
struct refill_mark
{
    long long refilled; 
    long long refill_ms; //mili-second since the creation of structure bulk_token_bucket;
};

struct bulk_token_bucket
{
	uuid_t my_id;
    struct btb_configuration cfg;
    struct timeval start;
    long long perturb;
	struct timeval perturb_timestamp;

    struct ST_hyperloglog *hll; //counting active bucket id with a sliding window style
	struct g_array *consumed;
    struct refill_mark *refilled;
};
static double collision_probability(long long bucket_num, long long n_key)
{
    //https://en.wikipedia.org/wiki/Birthday_problem#Number_of_people_with_a_shared_birthday
  	double p_non_collsion=1-pow((double)(bucket_num-1)/bucket_num, n_key-1);
    return p_non_collsion;
}

struct bulk_token_bucket *bulk_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long bucket_num)
{
    struct bulk_token_bucket *btb=ALLOC(struct bulk_token_bucket, 1);
    btb->cfg.CIR=CIR;
    btb->cfg.CBS=CBS;
    btb->cfg.bucket_num=bucket_num;
    btb->cfg.refill_interval_ms=10;
    btb->hll=ST_hyperloglog_new(6, 10, now);
    memcpy(&btb->start, &now, sizeof(btb->start));
    btb->consumed=g_array_new(my_id, bucket_num);
    btb->refilled=ALLOC(struct refill_mark, bucket_num);
	btb->perturb=1;
	memcpy(&btb->perturb_timestamp, &now, sizeof(btb->perturb_timestamp));
	memcpy(&btb->start, &now, sizeof(btb->start));
    uuid_copy(btb->my_id, my_id);
    return btb;
}
void bulk_token_bucket_free(struct bulk_token_bucket *btb)
{
	g_array_free(btb->consumed);
	ST_hyperloglog_free(btb->hll);
    free(btb->refilled);
    free(btb);
}

long long bulk_token_bucket_control(struct bulk_token_bucket *btb, const char *key, size_t keylen, struct timeval now, enum token_bucket_command cmd, long long tokens)
{
	long long delta_time_ms=0, bucket_idx=0, consumed=0, refilled=0;
	long long now_ms=timeval_delta_ms(btb->start, now);
	delta_time_ms=timeval_delta_ms(btb->perturb_timestamp, now);
	ST_hyperloglog_add(btb->hll, key, keylen, now);
	//Perturb every CBS/CIR seconds, so that buckets not used after last perturb have been refilled to CBS. 
	if(delta_time_ms > MIN(1000*btb->cfg.CBS/btb->cfg.CIR, PERTURB_INTERVAL_MAX_MS))
	{
		btb->perturb++;
		memcpy(&btb->perturb_timestamp, &now, sizeof(btb->perturb_timestamp));
	}
	
	bucket_idx=XXH3_64bits_withSeed(key, keylen, btb->perturb)%btb->cfg.bucket_num;
	consumed=g_array_get(btb->consumed, bucket_idx);
	refilled=btb->refilled[bucket_idx].refilled;
	assert(refilled>=0);
	assert(consumed>=0);

	long long perturb_ms=timeval_delta_ms(btb->start, btb->perturb_timestamp);

	//If current bucket hasn't been refilled since latest perturb.
	//find the previous bucket that the key used.
	//set the current bucket to the same available tokens as previous bucket.
	if(btb->refilled[bucket_idx].refill_ms < perturb_ms)
	{
		long long pre_bucket_idx=0;
		pre_bucket_idx=XXH3_64bits_withSeed(key, keylen, btb->perturb-1)%btb->cfg.bucket_num;
		if(btb->refilled[pre_bucket_idx].refill_ms < perturb_ms &&
				pre_bucket_idx!=bucket_idx)
		{
			long long pre_consumed=0, pre_refilled=0;
			pre_consumed=g_array_get(btb->consumed, pre_bucket_idx);
			pre_refilled=btb->refilled[pre_bucket_idx].refilled;
			long long pre_available=0, pre_new_refilled=0;
			pre_available=tb_available(btb->cfg.CIR, btb->cfg.CBS, 
										pre_consumed, pre_refilled, 
										btb->refilled[pre_bucket_idx].refill_ms, now_ms, btb->cfg.refill_interval_ms, 
										&pre_new_refilled);

			btb->refilled[bucket_idx].refilled  += pre_available - (btb->refilled[bucket_idx].refilled - consumed);
			btb->refilled[bucket_idx].refill_ms = now_ms;
			refilled = btb->refilled[bucket_idx].refilled;
		}
	}

	long long new_refilled=0, global_available=0;
	int infinite_flag=0;
    if(btb->cfg.CBS==0 && btb->cfg.CIR==0)
    {
		infinite_flag=1;
		new_refilled += tokens;
    }
	else
	{
		global_available=tb_available(btb->cfg.CIR, btb->cfg.CBS, consumed, refilled, btb->refilled[bucket_idx].refill_ms, now_ms, btb->cfg.refill_interval_ms, &new_refilled);
	}

	size_t n_replica=MAX(1, g_array_replicas(btb->consumed));

	long long reserved=btb->cfg.CIR*(n_replica-1)/n_replica;
	long long local_available=MAX(global_available-reserved, 0);
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
	if(new_refilled!=refilled)
	{
		btb->refilled[bucket_idx].refilled=new_refilled;
        btb->refilled[bucket_idx].refill_ms=timeval_delta_ms(btb->start, now);
	}
	assert(assigned>=0);
	if(assigned) g_array_incrby(btb->consumed, bucket_idx, assigned);
	return assigned;
}
void bulk_token_bucket_configure(struct bulk_token_bucket *btb, struct timeval now, long long CIR, long long CBS, long long bucket_num)
{
    btb->cfg.CIR=CIR;
    btb->cfg.CBS=CBS;
    btb->cfg.bucket_num=bucket_num;
    memcpy(&btb->cfg.timestamp, &now, sizeof(btb->cfg.timestamp));
 	g_array_resize(btb->consumed, bucket_num);
	btb->refilled=realloc(btb->refilled, btb->cfg.bucket_num*sizeof(struct refill_mark));
    return;
}
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct bulk_token_bucket_info *info)
{
	info->CIR=btb->cfg.CIR;
	info->CBS=btb->cfg.CBS;
	info->bucket_number=btb->cfg.bucket_num;
	info->replicas=g_array_replicas(btb->consumed);
	info->estimate_keys=ST_hyperloglog_count(btb->hll);
	info->collision_rate=collision_probability(info->bucket_number, info->estimate_keys);
	return;
}
const size_t  BULK_TOKEN_BUCKET_HEADER_SIZE=offsetof(struct bulk_token_bucket, hll);
void bulk_token_bucket_serialize(const struct bulk_token_bucket *btb, char **blob, size_t *blob_sz)
{
	size_t sz=0, offset=0;
	sz += BULK_TOKEN_BUCKET_HEADER_SIZE;
	sz += ST_hyperloglog_serialized_size(btb->hll);
	sz += g_array_serialized_size(btb->consumed);
	sz += btb->cfg.bucket_num*sizeof(struct refill_mark);
	
	char *buffer = ALLOC(char, sz);
	memcpy(buffer+offset, btb, BULK_TOKEN_BUCKET_HEADER_SIZE);
	offset+=BULK_TOKEN_BUCKET_HEADER_SIZE;
	char *tmp_blob=NULL;
	size_t tmp_sz=0;

	ST_hyperloglog_serialize(btb->hll, &tmp_blob, &tmp_sz);
	memcpy(buffer+offset, tmp_blob, tmp_sz);
	offset+=tmp_sz;
	free(tmp_blob);
	tmp_blob=NULL;

	g_array_serialize(btb->consumed, &tmp_blob, &tmp_sz);
	memcpy(buffer+offset, tmp_blob, tmp_sz);
	offset+=tmp_sz;
	free(tmp_blob);
	tmp_blob=NULL;

	memcpy(buffer+offset, btb->refilled, btb->cfg.bucket_num*sizeof(struct refill_mark));
	offset+=btb->cfg.bucket_num*sizeof(struct refill_mark);
	
	*blob=buffer;
	*blob_sz=sz;

	assert(offset==sz);
	return;
}
struct bulk_token_bucket *bulk_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct bulk_token_bucket *btb=ALLOC(struct bulk_token_bucket, 1);
	size_t offset=0;
	memcpy(btb, blob+offset, BULK_TOKEN_BUCKET_HEADER_SIZE);
	offset+=BULK_TOKEN_BUCKET_HEADER_SIZE;
	btb->hll=ST_hyperloglog_deserialize(blob+offset, blob_sz-offset);
	offset+=ST_hyperloglog_serialized_size(btb->hll);
	btb->consumed=g_array_deserialize(blob+offset, blob_sz-offset);
	offset+=g_array_serialized_size(btb->consumed);
	btb->refilled=ALLOC(struct refill_mark, btb->cfg.bucket_num);
	memcpy(btb->refilled, blob+offset,  btb->cfg.bucket_num*sizeof(struct refill_mark));
	offset+=btb->cfg.bucket_num*sizeof(struct refill_mark);

	assert(offset==blob_sz);
	return btb;
}
void bulk_token_bucket_merge(struct bulk_token_bucket *dst, const struct bulk_token_bucket *src)
{
    if(timercmp(&(dst->cfg.timestamp), &(src->cfg.timestamp), <))//Last-Write-Wins
    {
		bulk_token_bucket_configure(dst, src->cfg.timestamp, src->cfg.CIR, src->cfg.CBS, src->cfg.bucket_num);
    }
	//Stop to proceed when src is older than dst, and has a different bucket_num.
	if(dst->cfg.bucket_num != src->cfg.bucket_num) return;
	ST_hyperloglog_merge(dst->hll, src->hll);
	g_array_merge(dst->consumed, src->consumed);
	for(long long i=0; i<dst->cfg.bucket_num; i++)
	{
		if(dst->refilled[i].refilled < src->refilled[i].refilled)//Bigger refilled wins
		{
			dst->refilled[i].refilled=src->refilled[i].refilled;
			dst->refilled[i].refill_ms=src->refilled[i].refill_ms;
		}
	}
	return;
}
void bulk_token_bucket_merge_blob(struct bulk_token_bucket *btb, const char * blob, size_t blob_sz)
{
	struct bulk_token_bucket *src=bulk_token_bucket_deserialize(blob, blob_sz);
	bulk_token_bucket_merge(btb, src);
	bulk_token_bucket_free(src);
	return;
}