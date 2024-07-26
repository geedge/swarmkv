/*
The cuckoo bucket idea is based Cuckoo hashing (https://en.wikipedia.org/wiki/Cuckoo_hashing). 
*/
#include "bulk_token_bucket.h"
#include "pn_counter.h"
#include "crdt_utils.h"

#include "xxhash.h"
#include "uthash.h"
#include "mpack.h"

#include <math.h>
#include <assert.h>


#define BTB_ST_HLL_PRECISION 7
#define BTB_ST_HLL_WINDOW_MIN_MS 10000

struct btb_configuration
{
	long long rate;
	long long period;
	long long capacity;
	long long refill_interval_ms;
    long long initial_bucket_num;
	struct timeval last_cfg;
};
struct cuckoo_bucket
{
	XXH128_hash_t hash; 
    long long refilled; 
    long long refill_ms; //mili-second since the creation of structure bulk_token_bucket;
	union
	{
		struct PN_counter *consumed;
		size_t pnc_size;	//Used as part of serialization header
	};
};
struct cuckoo_bucket *cuckoo_bucket_new(const XXH128_hash_t hash, uuid_t my_id, long long capacity, long long now_ms)
{
	struct cuckoo_bucket *btb=ALLOC(struct cuckoo_bucket, 1);
	btb->hash = hash;
	btb->refilled = capacity;
	btb->refill_ms = now_ms;
	btb->consumed = PN_counter_new(my_id);
	return btb;
}
void cuckoo_bucket_free(struct cuckoo_bucket *bucket)
{
	PN_counter_free(bucket->consumed);
	free(bucket);
	return;
}
size_t cuckoo_bucket_mem_size(const struct cuckoo_bucket *bucket)
{
	size_t sz=0;
	sz += sizeof(struct cuckoo_bucket);
	sz += PN_counter_mem_size(bucket->consumed);
	return sz;
}
int cuckoo_bucket_is_stale(struct cuckoo_bucket *bucket, const struct btb_configuration *cfg, long long now_ms)
{
	long long delta_ms = now_ms - bucket->refill_ms;
	if(delta_ms > 2*cfg->capacity*cfg->period*1000/cfg->rate)
		return 1;
	return 0;
}
size_t cuckoo_bucket_serialize_size(const struct cuckoo_bucket *bucket)
{
	size_t sz=0;
	sz += sizeof(struct cuckoo_bucket);
	sz += PN_counter_serialized_size(bucket->consumed);
	return sz;
}
void cuckoo_bucket_serialize(const struct cuckoo_bucket *bucket, uint64_t index, char **blob, size_t *blob_sz)
{
	size_t sz=0, offset=0;
	sz = cuckoo_bucket_serialize_size(bucket);
	char *buffer = ALLOC(char, sz);
	struct cuckoo_bucket *hdr = (struct cuckoo_bucket*) buffer;
	memcpy(hdr, bucket, sizeof(struct cuckoo_bucket));
	hdr->pnc_size = PN_counter_serialized_size(bucket->consumed);
	offset += sizeof(struct cuckoo_bucket);

	char *tmp_blob=NULL;
	size_t tmp_sz=0;
	PN_counter_serialize(bucket->consumed, &tmp_blob, &tmp_sz);
	memcpy(buffer+offset, tmp_blob, tmp_sz);
	offset += tmp_sz;
	free(tmp_blob);
	*blob = buffer;
	*blob_sz = sz;
	assert(sz == offset);
	return;
}

struct cuckoo_bucket *cuckoo_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct cuckoo_bucket *bucket = ALLOC(struct cuckoo_bucket, 1);
	struct cuckoo_bucket *hdr = (struct cuckoo_bucket*) blob;
	assert(blob_sz >= sizeof(struct cuckoo_bucket) + hdr->pnc_size);
	memcpy(bucket, hdr, sizeof(struct cuckoo_bucket));
	bucket->consumed = PN_counter_deserialize(blob+sizeof(struct cuckoo_bucket), hdr->pnc_size);
	return bucket;
}
void cuckoo_bucket_merge(struct cuckoo_bucket *dst, const struct cuckoo_bucket *src)
{
	assert(XXH128_isEqual(dst->hash, src->hash));
	long long old_consumed = PN_counter_get(dst->consumed);
	PN_counter_merge(dst->consumed, src->consumed);
	long long new_consumed = PN_counter_get(dst->consumed);
	assert(new_consumed >= old_consumed);
	if(dst->refill_ms < src->refill_ms)//Last-Write-Wins
	{
		dst->refill_ms = src->refill_ms;
		dst->refilled = src->refilled;
	}
	return;
}
struct bulk_token_bucket
{
    struct btb_configuration cfg;
	uuid_t uuid;
	long long bucket_num;
	struct timeval last_consume;
	struct cuckoo_bucket **table;
	long long kickout_count;
	long long new_bucket_count;
};
static int isPower2(uint64_t num) { return (num & (num - 1)) == 0 && num != 0; }
struct bulk_token_bucket *bulk_token_bucket_new(uuid_t my_id, struct timeval now, long long rate, long long period, long long capacity, long long bucket_num)
{
    struct bulk_token_bucket *btb = ALLOC(struct bulk_token_bucket, 1);
	if(rate<0 || period<0 || capacity<0 || bucket_num<0 || !isPower2(bucket_num)) return NULL;
    btb->cfg.rate = rate;
    btb->cfg.period = period;
	btb->cfg.capacity = capacity;
    btb->cfg.initial_bucket_num = bucket_num;
    btb->cfg.refill_interval_ms = 10;
	btb->cfg.last_cfg = now;
	uuid_copy(btb->uuid, my_id);
	btb->bucket_num = bucket_num;
    btb->table = ALLOC(struct cuckoo_bucket*, bucket_num);
    return btb;
}
void bulk_token_bucket_free(struct bulk_token_bucket *btb)
{
	assert(btb->bucket_num >= btb->cfg.initial_bucket_num);
	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i]) continue;		
		cuckoo_bucket_free(btb->table[i]);
		btb->table[i] = NULL;
	}
    free(btb->table);
    free(btb);
}
#define	CUCKOO_HASH_SEED	52103867	//Big prime number
#define	CUCKOO_MAX_INTERATIONS	8
#define CUCKOO_TABLE_EXPANSION_FACTOR 2
#define	CUCKOO_TABLE_MAX_SIZE	(1<<24)
//Source: https://github.com/efficient/cuckoofilter/blob/master/src/cuckoofilter.h#L73
static uint64_t hash2index(const XXH128_hash_t hash, long long bucket_num)
{
	return hash.low64%bucket_num;
}
static uint64_t hash2alt_index(const XXH128_hash_t hash, uint64_t index, long long bucket_num)
{
	return (index^(hash.high64*0x5bd1e995))%bucket_num;
}
static struct cuckoo_bucket *btb_lookup(const struct bulk_token_bucket *btb, XXH128_hash_t hash)
{
	struct cuckoo_bucket *bkt=NULL;
	uint64_t i1=hash2index(hash, btb->bucket_num);
	uint64_t i2=hash2alt_index(hash, i1, btb->bucket_num);
	if(btb->table[i1] && XXH128_isEqual(btb->table[i1]->hash, hash))
	{
		bkt=btb->table[i1];
	}
	if(btb->table[i2] && XXH128_isEqual(btb->table[i2]->hash, hash))
	{
		bkt=btb->table[i2];
	}
	return bkt; 
}

static struct cuckoo_bucket *cuckoo_table_insert(struct cuckoo_bucket **table, long long bucket_num, const struct btb_configuration *cfg, long long now_ms, struct cuckoo_bucket *bkt, int *retired_cnt)
{
	*retired_cnt = 0;
	uint64_t i1 = hash2index(bkt->hash, bucket_num);
	uint64_t i2 = hash2alt_index(bkt->hash, i1, bucket_num);
	if(table[i1] == NULL)
	{
		table[i1] = bkt;
		return NULL;
	}
	if(table[i2] == NULL)
	{
		table[i2] = bkt;
		return NULL;
	}
	assert(!XXH128_isEqual(table[i1]->hash, bkt->hash) && !XXH128_isEqual(table[i2]->hash, bkt->hash));
	//randomly choose a victim to relocated.
	long long alt_idx = (bkt->hash.high64^bkt->hash.low64)%2 ? i1 : i2;
	struct cuckoo_bucket *victim = table[alt_idx];
	table[alt_idx] = bkt;
	for(int i=0; i<CUCKOO_MAX_INTERATIONS; i++)
	{
		//swap victim with the bucket in the alt_idx.
		alt_idx = hash2alt_index(victim->hash, alt_idx, bucket_num);
		struct cuckoo_bucket *tmp = table[alt_idx];
		table[alt_idx] = victim;
		victim = NULL;
		if(tmp == NULL)
		{
			break;
		}
		//A new insert bucket with the refill_ms = 0, may be considered as stale
		else if(cuckoo_bucket_is_stale(tmp, cfg, now_ms) && tmp != bkt)
		{
			//printf("retire %lld %p\n", alt_idx, tmp);
			cuckoo_bucket_free(tmp);
			(*retired_cnt)++;
			break;
		}
		else
		{
			victim = tmp;
		}
	}
	return victim;
}
static int btb_expand(struct bulk_token_bucket *btb, long long now_ms)
{
	long long new_bucket_num = btb->bucket_num;
	struct cuckoo_bucket **new_table = NULL;
	int retire_cnt = 0;
do_expansion:
	new_bucket_num *= CUCKOO_TABLE_EXPANSION_FACTOR;
	if(new_bucket_num > CUCKOO_TABLE_MAX_SIZE) return -1;
	new_table=realloc(new_table, sizeof(struct cuckoo_bucket*)*new_bucket_num);
	memset(new_table, 0, sizeof(struct cuckoo_bucket*)*new_bucket_num);
	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i]) continue;
		if(cuckoo_bucket_is_stale(btb->table[i], &btb->cfg, now_ms))
		{
			cuckoo_bucket_free(btb->table[i]);
			continue;
		}
		struct cuckoo_bucket *victim = NULL;
		retire_cnt = 0;
		victim = cuckoo_table_insert(new_table, new_bucket_num, &btb->cfg, now_ms, btb->table[i], &retire_cnt);
		assert(retire_cnt == 0);
		if(victim) 
			goto do_expansion;
	}
	free(btb->table);
	btb->table = new_table;
	btb->bucket_num = new_bucket_num;
	
	return 0;
}
static void btb_insert(struct bulk_token_bucket *btb, struct cuckoo_bucket *bkt, long long now_ms)
{
	struct cuckoo_bucket *victim = NULL;
	int retire_cnt = 0;
	victim = cuckoo_table_insert(btb->table, btb->bucket_num, &btb->cfg, now_ms, bkt, &retire_cnt);
	retire_cnt = 0;
	if(victim)
	{
		if(0 == btb_expand(btb, now_ms))
		{
			cuckoo_table_insert(btb->table, btb->bucket_num, &btb->cfg, now_ms, victim, &retire_cnt);
			assert(retire_cnt == 0);
		}
		else
		{
			cuckoo_bucket_free(victim);
			btb->kickout_count++;
		}
	}
	return;
}
long long bulk_token_bucket_consume(struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t keylen, enum tb_consume_type cmd, long long tokens)
{
	long long now_ms = timeval_delta_ms(btb->cfg.last_cfg, now);
	struct cuckoo_bucket *bkt = NULL;
	btb->last_consume = now;
	XXH128_hash_t hash;
	hash = XXH3_128bits_withSeed(key, keylen, CUCKOO_HASH_SEED);
	bkt = btb_lookup(btb, hash);
	if(!bkt)
	{
		bkt = cuckoo_bucket_new(hash, btb->uuid, btb->cfg.capacity, now_ms);
		btb_insert(btb, bkt, now_ms);
		btb->new_bucket_count++;
	}

	long long consumed=0, global_available=0;
	int infinite_flag=0;
	consumed = PN_counter_get(bkt->consumed);
    if(btb->cfg.period == 0)
    {
		infinite_flag = 1;
		bkt->refill_ms = now_ms;
		bkt->refilled += tokens;
	}
	else
	{
		global_available = tb_available(btb->cfg.rate, btb->cfg.period, btb->cfg.capacity, btb->cfg.refill_interval_ms,
										consumed, &bkt->refilled, now_ms, &bkt->refill_ms);
	}
	size_t n_replica = MAX(1, PN_counter_replica_num(bkt->consumed));
	long long assigned=0;
	if(infinite_flag)
	{
		assigned = tokens;
	}
	else
	{
		assigned = tb_consume_reserve_based(btb->cfg.rate, btb->cfg.period, global_available, btb->cfg.capacity, n_replica, cmd, tokens);
	}
	assert(assigned>=0);
	if(assigned) PN_counter_incrby(bkt->consumed, assigned);
	return assigned;
}
void bulk_token_bucket_configure(struct bulk_token_bucket *btb, struct timeval now, long long rate, long long period, long long capacity, long long bucket_num)
{
    btb->cfg.rate = rate;
	btb->cfg.period = period;
    btb->cfg.capacity = capacity;
    btb->cfg.last_cfg = now;

	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i])
		{
			continue;
		}
		cuckoo_bucket_free(btb->table[i]);
	}
	free(btb->table);

	btb->cfg.initial_bucket_num = bucket_num;
	btb->bucket_num = bucket_num;
	
	btb->table = ALLOC(struct cuckoo_bucket*, bucket_num);;
    return;
}
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct timeval now, struct bulk_token_bucket_info *info)
{
	memset(info, 0, sizeof(struct bulk_token_bucket_info));
	info->rate = btb->cfg.rate;
	info->period = btb->cfg.period;
	info->capacity = btb->cfg.capacity;
	info->initial_bucket_number = btb->cfg.initial_bucket_num;
	info->bucket_number = btb->bucket_num;
	info->max_replicas = 0;
	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i]) continue;
		if(cuckoo_bucket_is_stale(btb->table[i], &btb->cfg, timeval_delta_ms(btb->cfg.last_cfg, now)))
		{
			continue;
		}
		info->max_replicas = MAX(info->max_replicas, PN_counter_replica_num(btb->table[i]->consumed));
		info->active_bucket_number++;
	}
	return;
}

void bulk_token_bucket_query(const struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t keylen, struct bulk_token_bucket_key_info *key_info)
{	
	XXH128_hash_t hash;
	hash = XXH3_128bits_withSeed(key, keylen, CUCKOO_HASH_SEED);

	memset(key_info, 0, sizeof(struct bulk_token_bucket_key_info));
	key_info->hash_low64 = hash.low64;
	key_info->hash_high64 = hash.high64;

	struct cuckoo_bucket *bkt = btb_lookup(btb, hash);
	if(!bkt)
	{
		key_info->consumed = 0;
		key_info->refilled = btb->cfg.capacity;
		key_info->available = (btb->cfg.period == 0) ? INT64_MAX : btb->cfg.capacity;
	}
	else
	{
		long long now_ms = timeval_delta_ms(btb->cfg.last_cfg, now);
		long long refill_ms = bkt->refill_ms;
		key_info->consumed = PN_counter_get(bkt->consumed);
		key_info->refilled = bkt->refilled;
		key_info->available = tb_available(btb->cfg.rate, btb->cfg.period, btb->cfg.capacity, btb->cfg.refill_interval_ms, 
					key_info->consumed, &key_info->refilled, 
					now_ms, &refill_ms);
		key_info->available = (btb->cfg.period == 0) ? INT64_MAX : key_info->available;
		key_info->number_of_replica = PN_counter_replica_num(bkt->consumed);
		key_info->last_consume_ms = btb->cfg.last_cfg.tv_sec*1000 + btb->cfg.last_cfg.tv_usec/1000 + bkt->refill_ms;
	}
	return;
}
const size_t  BULK_TOKEN_BUCKET_HEADER_SIZE=offsetof(struct bulk_token_bucket, table);

void bulk_token_bucket_serialize(const struct bulk_token_bucket *btb, char **blob, size_t *blob_sz)
{
	size_t sz=0, offset=0;
	long long now_ms = timeval_delta_ms(btb->cfg.last_cfg, btb->last_consume);
	sz += BULK_TOKEN_BUCKET_HEADER_SIZE;
	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i]) continue;
		if(cuckoo_bucket_is_stale(btb->table[i], &btb->cfg, now_ms))
		{
			continue;
		}
		sz += cuckoo_bucket_serialize_size(btb->table[i]);
	}
	char *buffer = ALLOC(char, sz);
	memcpy(buffer+offset, btb, BULK_TOKEN_BUCKET_HEADER_SIZE);
	offset += BULK_TOKEN_BUCKET_HEADER_SIZE;
	char *tmp_blob = NULL;
	size_t tmp_sz = 0;

	memcpy(buffer+offset, tmp_blob, tmp_sz);
	offset += tmp_sz;
	free(tmp_blob);
	tmp_blob=NULL;

	for(int i=0; i<btb->bucket_num; i++)
	{
		if(!btb->table[i])	continue;		
		//We skip the stale bucket.
		if(cuckoo_bucket_is_stale(btb->table[i], &btb->cfg, now_ms))
		{
			continue;
		}
		//We do not skip the full bucket, since full bucket can also been consumed.
		/*
		if(cuckoo_bucket_read_avialable(btb->table[i], &btb->cfg, now_ms)==btb->cfg.capacity)
		{
			printf("skip full bucket %d\n", i);
			continue;
		}
		*/
		cuckoo_bucket_serialize(btb->table[i], i, &tmp_blob, &tmp_sz);
		memcpy(buffer+offset, tmp_blob, tmp_sz);
		offset += tmp_sz;
		free(tmp_blob);
		tmp_blob=NULL;
	}
	*blob = buffer;
	*blob_sz = sz;

	assert(offset==sz);
	return;
}
struct bulk_token_bucket *bulk_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct bulk_token_bucket *btb=ALLOC(struct bulk_token_bucket, 1);
	size_t offset=0;
	memcpy(btb, blob+offset, BULK_TOKEN_BUCKET_HEADER_SIZE);
	offset += BULK_TOKEN_BUCKET_HEADER_SIZE;
	btb->table = ALLOC(struct cuckoo_bucket*, btb->bucket_num);
	while(offset < blob_sz)
	{
		struct cuckoo_bucket *bucket=NULL;
		bucket = cuckoo_bucket_deserialize(blob+offset, blob_sz-offset);
		offset += cuckoo_bucket_serialize_size(bucket);
		btb_insert(btb, bucket, bucket->refill_ms);
	}
	assert(offset == blob_sz);
	return btb;
}
void bulk_token_bucket_merge(struct bulk_token_bucket *dst, const struct bulk_token_bucket *src)
{
    if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), <))//Last-Write-Wins
    {
		bulk_token_bucket_configure(dst, src->cfg.last_cfg, src->cfg.rate, src->cfg.period, src->cfg.capacity, src->cfg.initial_bucket_num);
    }
	assert(isPower2(dst->bucket_num) && isPower2(src->bucket_num));
	long long new_insert=0;
	for(int i=0; i<src->bucket_num; i++)
	{
		if(!src->table[i]) continue;
		struct cuckoo_bucket *dst_bkt = btb_lookup(dst, src->table[i]->hash);
		if(dst_bkt)
		{
			cuckoo_bucket_merge(dst_bkt, src->table[i]);
		}
		else
		{
			//The dst bucket should has smaller refill timestamp than the src bucket, so that can be merged to src refill tokens.
			dst_bkt = cuckoo_bucket_new(src->table[i]->hash, dst->uuid, dst->cfg.capacity, src->table[i]->refill_ms-1);
			cuckoo_bucket_merge(dst_bkt, src->table[i]);
			btb_insert(dst, dst_bkt, src->table[i]->refill_ms);
			new_insert++;
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
	struct bulk_token_bucket *src=bulk_token_bucket_deserialize(blob, blob_sz);
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, src->cfg.last_cfg, src->cfg.rate, src->cfg.period, src->cfg.capacity, src->cfg.initial_bucket_num);
	bulk_token_bucket_merge(btb, src);
	bulk_token_bucket_free(src);
	return btb;
}
size_t bulk_token_bucket_mem_size(const struct bulk_token_bucket *btb)
{
	size_t sz=0;
	sz += sizeof(struct bulk_token_bucket);
	sz += sizeof(struct cuckoo_bucket*)*btb->bucket_num;
	for(int i=0; i<btb->bucket_num; i++)
	{
		if(btb->table[i])
		{
			sz+=cuckoo_bucket_mem_size(btb->table[i]);
		}
	}
    return sz;
}