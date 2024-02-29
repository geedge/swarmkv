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
#define BTB_ST_HLL_PRECISION 7
#define BTB_ST_HLL_WINDOW_MIN_MS 1000

struct btb_configuration
{
	long long rate;
	long long period;
	long long capacity;
	long long refill_interval_ms;
    long long bucket_num;
	struct timeval last_cfg;
};
struct refill_mark
{
    long long refilled; 
    long long refill_ms; //mili-second since the creation of structure bulk_token_bucket;
};

struct bulk_token_bucket
{
    struct btb_configuration cfg;
    struct timeval start;
    long long perturb;
	struct timeval last_perturb;

    struct ST_hyperloglog *hll; //counting active bucket id with a sliding window style
	struct g_array *consumed;
    struct refill_mark *refilled;
};
static double collision_probability(long long bucket_num, long long n_key)
{
	if(n_key==0) return 0;
    //https://en.wikipedia.org/wiki/Birthday_problem#Number_of_people_with_a_shared_birthday
  	double p_non_collsion=1-pow((double)(bucket_num-1)/bucket_num, n_key-1);
    return p_non_collsion;
}

struct bulk_token_bucket *bulk_token_bucket_new(uuid_t my_id, struct timeval now, long long rate, long long period, long long capacity, long long bucket_num)
{
    struct bulk_token_bucket *btb=ALLOC(struct bulk_token_bucket, 1);
	if(rate<0 || period<0 || capacity<0 || bucket_num<0) return NULL;
    btb->cfg.rate = rate;
    btb->cfg.period = period;
	btb->cfg.capacity = capacity;
    btb->cfg.bucket_num = bucket_num;
    btb->cfg.refill_interval_ms = 10;
	btb->cfg.last_cfg = now;
	long long sthll_time_window_ms=MAX(BTB_ST_HLL_WINDOW_MIN_MS, period*1000);
    btb->hll=ST_hyperloglog_new(BTB_ST_HLL_PRECISION, sthll_time_window_ms, now);
    btb->start = now;
    btb->consumed=g_array_new(my_id, bucket_num);
    btb->refilled=ALLOC(struct refill_mark, bucket_num);
	btb->perturb=1;
	btb->last_perturb = now;
    return btb;
}
void bulk_token_bucket_free(struct bulk_token_bucket *btb)
{
	g_array_free(btb->consumed);
	ST_hyperloglog_free(btb->hll);
    free(btb->refilled);
    free(btb);
}

long long bulk_token_bucket_consume(struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t keylen, enum tb_consume_type cmd, long long tokens)
{
	long long delta_time_ms=0, bucket_idx=0, consumed=0, refilled=0;
	long long now_ms=timeval_delta_ms(btb->start, now);
	delta_time_ms=timeval_delta_ms(btb->last_perturb, now);
	ST_hyperloglog_add(btb->hll, key, keylen, now);
	//Perturb every CBS/CIR seconds, so that buckets not used after last perturb have been refilled to CBS. 
	if(!btb->cfg.bucket_num) return 0;
	long long perturb_threshold_ms=btb->cfg.period*1000*btb->cfg.capacity/MAX(btb->cfg.rate, 1);
	if(btb->cfg.rate && delta_time_ms > MIN(perturb_threshold_ms, PERTURB_INTERVAL_MAX_MS))
	{
		btb->perturb++;
		memcpy(&btb->last_perturb, &now, sizeof(btb->last_perturb));
	}
	
	bucket_idx=XXH3_64bits_withSeed(key, keylen, btb->perturb)%btb->cfg.bucket_num;
	consumed=g_array_get(btb->consumed, bucket_idx);
	refilled=btb->refilled[bucket_idx].refilled;
	assert(refilled>=0);
	assert(consumed>=0);

	long long perturb_ms=timeval_delta_ms(btb->start, btb->last_perturb);

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
			pre_available=tb_available(btb->cfg.rate, btb->cfg.period, btb->cfg.capacity, 
										pre_consumed, pre_refilled, 
										now_ms - btb->refilled[pre_bucket_idx].refill_ms, btb->cfg.refill_interval_ms, 
										&pre_new_refilled);

			btb->refilled[bucket_idx].refilled  += pre_available - (btb->refilled[bucket_idx].refilled - consumed);
			btb->refilled[bucket_idx].refill_ms = now_ms;
			refilled = btb->refilled[bucket_idx].refilled;
		}
	}

	long long new_refilled=0, global_available=0;
	int infinite_flag=0;
    if(btb->cfg.period==0)
    {
		infinite_flag=1;
		new_refilled += tokens;
    }
	else
	{
		long long delta_ms = now_ms - btb->refilled[bucket_idx].refill_ms;
		global_available = tb_available(btb->cfg.rate, btb->cfg.period, btb->cfg.capacity, consumed, refilled, delta_ms, btb->cfg.refill_interval_ms, &new_refilled);
	}

	size_t n_replica=MAX(1, g_array_replicas(btb->consumed));
	long long assigned=0;
	if(infinite_flag)
	{
		assigned=tokens;
	}
	else
	{
		assigned=tb_consume_reserve_based(btb->cfg.rate, btb->cfg.period, global_available, btb->cfg.capacity, n_replica, cmd, tokens);
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
void bulk_token_bucket_configure(struct bulk_token_bucket *btb, struct timeval now, long long rate, long long period, long long capacity, long long bucket_num)
{
    btb->cfg.rate = rate;
	btb->cfg.period = period;
    btb->cfg.capacity = capacity;
    btb->cfg.bucket_num=bucket_num;
    btb->cfg.last_cfg = now;
	long long sthll_time_window_ms=MAX(BTB_ST_HLL_WINDOW_MIN_MS, period*1000);
	ST_hyperloglog_configure(btb->hll, BTB_ST_HLL_PRECISION, sthll_time_window_ms, now);
 	g_array_resize(btb->consumed, bucket_num);
	btb->refilled=realloc(btb->refilled, btb->cfg.bucket_num*sizeof(struct refill_mark));
	memset(btb->refilled, 0, btb->cfg.bucket_num*sizeof(struct refill_mark));
    return;
}
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct timeval now, struct bulk_token_bucket_info *info)
{
	info->rate = btb->cfg.rate;
	info->period = btb->cfg.period;
	info->capacity = btb->cfg.capacity;
	info->bucket_number = btb->cfg.bucket_num;
	info->replicas = g_array_replicas(btb->consumed);
	info->approximate_keys=ST_hyperloglog_count(btb->hll, now);
	info->collision_rate=collision_probability(info->bucket_number, info->approximate_keys);
	return;
}
long long bulk_token_bucket_read_available(const struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t keylen)
{
	long long consumed=0, refilled=0, available=0, new_refilled=0;
	int bucket_idx=0;
	bucket_idx = XXH3_64bits_withSeed(key, keylen, btb->perturb)%btb->cfg.bucket_num;
	consumed = g_array_get(btb->consumed, bucket_idx);
	refilled = btb->refilled[bucket_idx].refilled;
	long long now_ms = timeval_delta_ms(btb->start, now);
	long long delta_ms = now_ms - btb->refilled[bucket_idx].refill_ms;
	available=tb_available(btb->cfg.rate, btb->cfg.period, btb->cfg.capacity, consumed, refilled, delta_ms, btb->cfg.refill_interval_ms, &new_refilled);
	if(btb->cfg.rate==0 && btb->cfg.capacity==0) available=INT64_MAX;
	return available;
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
    if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), <))//Last-Write-Wins
    {
		bulk_token_bucket_configure(dst, src->cfg.last_cfg, src->cfg.rate, src->cfg.period, src->cfg.capacity, src->cfg.bucket_num);
    }
    if(timercmp(&(dst->last_perturb), &(src->last_perturb), <))//Last-Write-Wins
	{
		dst->perturb=src->perturb;
		memcpy(&dst->last_perturb, &src->last_perturb, sizeof(dst->last_perturb));
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
struct bulk_token_bucket *bulk_token_bucket_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
	struct bulk_token_bucket *btb=bulk_token_bucket_deserialize(blob, blob_sz);
	struct g_array *tmp=btb->consumed;
	btb->consumed=g_array_new(uuid, btb->cfg.bucket_num);
	g_array_merge(btb->consumed, tmp);
	g_array_free(tmp);
	return btb;
}
size_t bulk_token_bucket_mem_size(const struct bulk_token_bucket *btb)
{
	//to do
	size_t sz=0;
	sz+=sizeof(struct bulk_token_bucket);
	sz+=ST_hyperloglog_mem_size(btb->hll);
	sz+=sizeof(struct refill_mark)*btb->cfg.bucket_num;
	sz+=g_array_mem_size(btb->consumed);
    return sz;
}