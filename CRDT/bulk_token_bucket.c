#include "bulk_token_bucket.h"
#include "st_hll.h"
#include "crdt_utils.h"

#include "xxhash.h"
#include "uthash.h"
#include "mpack.h"

#include <math.h>
#include <assert.h>

#define PERTURB_INTERVAL_MS  1000


struct counter_array
{
	uuid_t replica_id;
    long long array_sz;
	long long *array;
	UT_hash_handle hh;
};
struct g_array	//Grow only array
{
	uuid_t my_id;
	long long array_sz;
	struct counter_array *hash;
};
struct g_array *g_array_new(uuid_t my_id, long long array_sz)
{
	struct g_array *ga=ALLOC(struct g_array, 1);
	uuid_copy(ga->my_id, my_id);
	ga->array_sz=array_sz;
    return ga;
}
void g_array_free(struct g_array *ga)
{
	struct counter_array *item=NULL, *tmp=NULL;
	HASH_ITER(hh, ga->hash, item, tmp)
	{
		HASH_DELETE(hh, ga->hash, item);
		free(item->array);
		free(item);
	}
	free(ga);
	return;
}
long long g_array_get(const struct g_array *ga, long long idx)
{
	struct counter_array *item=NULL, *tmp=NULL;
	long long value=0;
	HASH_ITER(hh, ga->hash, item, tmp)
	{
        if(idx < item->array_sz)
            value += item->array[idx];
	}
	return value;
}
long long g_array_incrby(struct g_array *ga, long long idx, long long increment)
{
	struct counter_array *item=NULL;
	HASH_FIND(hh, ga->hash, ga->my_id, sizeof(ga->my_id), item);
	if(!item)
	{
		item=ALLOC(struct counter_array, 1);
		uuid_copy(item->replica_id, ga->my_id);
		item->array_sz=ga->array_sz;
		item->array=ALLOC(long long, item->array_sz);
		HASH_ADD_KEYPTR(hh, ga->hash, item->replica_id, sizeof(item->replica_id), item);
	}
	item->array[idx] += increment;
	return item->array[idx];
}
void g_array_resize(struct g_array *ga, long long new_size)
{
	struct counter_array *item=NULL;
	HASH_FIND(hh, ga->hash, ga->my_id, sizeof(ga->my_id), item);
	if(item && item->array_sz < new_size)
	{
        item->array=(long long *)realloc(item->array, sizeof(long long)*new_size);
        item->array_sz=new_size;
	}
	ga->array_sz=new_size;
    return;
}
size_t g_array_replicas(const struct g_array *ga)
{
	return HASH_COUNT(ga->hash);
}
struct btb_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_interval_ms;
    long long bucket_num;
	struct timeval write_timestamp;
};
struct refill_array
{
    long long refilled; 
    long long refill_ms; //mili-second since the creation of structure bulk_token_bucket;
};

struct bulk_token_bucket
{
    struct btb_configuration cfg;
    uuid_t my_id;
    struct timeval start;
    long long perturb;
	struct timeval perturb_timestamp;
	long long *snapshot;
    long long n_active_bucket;

    struct ST_HLL *hll; //counting active bucket id with a sliding window style
	struct g_array *consumed;
    struct refill_array *refilled;
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
    btb->hll=ST_HLL_new(6, 10, &now);
    memcpy(&btb->start, &now, sizeof(btb->start));
    btb->consumed=g_array_new(my_id, bucket_num);
    btb->refilled=ALLOC(struct refill_array, bucket_num);
	btb->perturb=1;
	btb->snapshot=ALLOC(long long, bucket_num);
	memcpy(&btb->perturb_timestamp, &now, sizeof(btb->perturb_timestamp));
	memcpy(&btb->start, &now, sizeof(btb->start));
    uuid_copy(btb->my_id, my_id);
    return btb;
}
void bulk_token_bucket_free(struct bulk_token_bucket *btb)
{
	g_array_free(btb->consumed);
	ST_HLL_free(btb->hll);
    free(btb->refilled);
	free(btb->snapshot);
    free(btb);
}

long long tb_available(long long CIR, long long CBS, long long consumed, long long refilled, long long last_refill_ms, long long now_ms, long long refill_interval_ms, long long *new_refilled)
{
	long long eplapse_ms = now_ms - last_refill_ms;
	long long to_add = CIR*eplapse_ms/1000;
	*new_refilled = refilled;
	if(refilled==0 && consumed==0)
	{
		*new_refilled=CBS;
		return *new_refilled;
	}
	if(eplapse_ms > refill_interval_ms && to_add>0)
	{
		if(refilled < consumed)
		{
			*new_refilled = consumed;
		}
		if(to_add + refilled - consumed < CBS)
		{
			*new_refilled += to_add;
		}
		else
		{
			*new_refilled += CBS - (refilled - consumed);
		}
	}
	return MAX(*new_refilled-consumed, 0);
}
long long bulk_token_bucket_control(struct bulk_token_bucket *btb, const char *key, size_t keylen, struct timeval now, enum bulk_token_bucket_command cmd, long long tokens)
{
	long long delta_time_ms=0, bucket_idx=0, consumed=0, refilled=0;
	long long now_ms=timeval_delta_ms(btb->start, now);
	delta_time_ms=timeval_delta_ms(btb->perturb_timestamp, now);
	ST_HLL_add(btb->hll, key, keylen, &now);
	//Perturb every CBS/CIR seconds, so that buckets not used after last perturb have been refilled to CBS. 
	if(delta_time_ms>1000*btb->cfg.CBS/btb->cfg.CIR)
	{
		btb->perturb++;
		long long new_refilled=0, tmp_consumed=0;
		memcpy(&btb->perturb_timestamp, &now, sizeof(btb->perturb_timestamp));
		for(int i=0; i<btb->cfg.bucket_num; i++)
		{
			tmp_consumed=g_array_get(btb->consumed, i);
			btb->snapshot[i]=tb_available(btb->cfg.CIR, btb->cfg.CBS, 
										tmp_consumed, btb->refilled[i].refilled, 
										btb->refilled[i].refill_ms, now_ms, btb->cfg.refill_interval_ms, 
										&new_refilled);
		}
	}
	
	bucket_idx=XXH3_64bits_withSeed(key, keylen, btb->perturb)%btb->cfg.bucket_num;
	consumed=g_array_get(btb->consumed, bucket_idx);
	refilled=btb->refilled[bucket_idx].refilled;
	assert(refilled>=0);
	assert(consumed>=0);

	long long perturb_ms=timeval_delta_ms(btb->start, btb->perturb_timestamp);

	if(0 && btb->refilled[bucket_idx].refill_ms < perturb_ms)
	{
		long long pre_bucket_idx=0;
		pre_bucket_idx=XXH3_64bits_withSeed(key, keylen, btb->perturb-1)%btb->cfg.bucket_num;
		btb->refilled[bucket_idx].refilled  += btb->snapshot[pre_bucket_idx] - (btb->refilled[bucket_idx].refilled - consumed);
		btb->refilled[bucket_idx].refill_ms = now_ms;
		refilled = btb->refilled[bucket_idx].refilled;		
	}
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
	if(0 && btb->refilled[bucket_idx].refill_ms < perturb_ms)
	{
		//find the previous bucket that the key used.
		long long pre_perturb=btb->perturb, pre_bucket_idx=0, tmp=0;
		long long rewind_max=1000*btb->cfg.CBS/btb->cfg.CIR/PERTURB_INTERVAL_MS;
		
		int rewind_success=0;
		do
		{
			pre_perturb--;
			tmp=XXH3_64bits_withSeed(key, keylen, pre_perturb)%btb->cfg.bucket_num;

			if(btb->refilled[tmp].refill_ms < perturb_ms - (btb->perturb - pre_perturb - 1) * PERTURB_INTERVAL_MS &&
			//if(btb->refilled[tmp].refill_ms < timeval_delta_ms(btb->start, btb->perturb_timestamp) &&
				tmp!=bucket_idx)
			{
				rewind_success=1;
				pre_bucket_idx=tmp;
				break;
			}
			else
			{
				break;
			}
			
		}while(btb->perturb - pre_perturb < rewind_max );
		
		if(rewind_success)
		{
			//set the current bucket to the same available tokens as previous bucket.
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
		case BTB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE:
			assigned=infinite_flag?tokens:global_available;
			break;
		case BTB_CMD_CONSUME_FORCE:
			assigned=tokens;
			break;
		case BTB_CMD_CONSUME_FLEXIBLE:
			assigned=infinite_flag?tokens:MIN(tokens, local_available);;
			break;
		case BTB_CMD_CONSUME_NORMAL:
			if(infinite_flag)
			{
				assigned=tokens;
			}
			else
			{
				assigned=(tokens<=local_available) ? tokens:0;
			}
			break;
		case BTB_CMD_READ_AVAILABLE:
			return global_available;
		case BTB_CMD_READ_REFILLED:
			return refilled;
		case BTB_CMD_READ_CONSUEMD:
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
    memcpy(&btb->cfg.write_timestamp, &now, sizeof(btb->cfg.write_timestamp));
 	g_array_resize(btb->consumed, bucket_num);
    return;
}
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct bulk_token_bucket_info *info)
{
	info->CIR=btb->cfg.CIR;
	info->CBS=btb->cfg.CBS;
	info->bucket_number=btb->cfg.bucket_num;
	info->replicas=g_array_replicas(btb->consumed);
	info->estimate_keys=ST_HLL_count(btb->hll);
	info->collision_rate=collision_probability(info->bucket_number, info->estimate_keys);
	return;
}
void bulk_token_bucket_serialize(struct OC_token_bucket *bucket, char **blob, size_t *blob_sz)
{

}