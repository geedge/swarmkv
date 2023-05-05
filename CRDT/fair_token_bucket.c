#include "fair_token_bucket.h"
#include "oc_token_bucket.h"
#include "st_hyperloglog.h"
#include "g_array.h"
#include "crdt_utils.h"
#include "xxhash.h"

#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>


#define REFILL_INTERVAL_MS  200


struct fair_token_bucket
{
    /* Sync Variables*/
    uuid_t uuid;
    long long divisor;
    long long perturb_seed;
    struct g_array *sfq;  //per-key deficit estimation
    struct ST_hyperloglog *hll[FAIR_TB_WEIGHT_MAX]; //counting active keys
    struct OC_token_bucket *bucket;
    
    /* Local Variables*/
    struct timeval last_refill_time;
    long long per_weight_quantum; //updated every REFILL_INTERVAL_MS
    long long n_active_key;   
};
struct fair_token_bucket *fair_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS, long long divisor)
{
    struct fair_token_bucket *ftb=ALLOC(struct fair_token_bucket, 1);

    uuid_copy(ftb->uuid, uuid);
    ftb->divisor=divisor;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ftb->hll[i]=ST_hyperloglog_new(9, 5, now);
    }
    ftb->sfq=g_array_new(uuid, ftb->divisor);
    ftb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    return ftb;
}
void fair_token_bucket_configure(struct fair_token_bucket *ftb, struct timeval now, long long CIR, long long CBS, long long divisor)
{
    if(divisor>ftb->divisor)
    {
        ftb->divisor=divisor;
        g_array_resize(ftb->sfq, ftb->divisor);
    }
    OC_token_bucket_configure(ftb->bucket, now, CIR, CBS);
    return;
}
void fair_token_bucket_free(struct fair_token_bucket *ftb)
{
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_free(ftb->hll[i]);
    }
    g_array_free(ftb->sfq);
    OC_token_bucket_free(ftb->bucket);
    free(ftb);
}

static void ftb_refill(struct fair_token_bucket *ftb, struct timeval now)
{
    long long delta_time_ms=timeval_delta_ms(ftb->last_refill_time, now);
    if(likely(delta_time_ms<REFILL_INTERVAL_MS))
    {
        return;
    }
    //Per weight quantum estimation
    long long total_weight=0, n_active_key=0, count=0;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        count = ST_hyperloglog_count(ftb->hll[i]);
        total_weight += (i+1)*count;
        n_active_key += count;
    }
    struct OC_token_bucket_info info;
    OC_token_bucket_info(ftb->bucket, now, &info);
    long long available=info.available;     
    ftb->per_weight_quantum=available/MAX(1, total_weight);
    ftb->n_active_key=n_active_key;
    
    memcpy(&ftb->last_refill_time, &now, sizeof(ftb->last_refill_time));
    g_array_reset(ftb->sfq);
    ftb->perturb_seed++;
}

long long fair_token_bucket_consume(struct fair_token_bucket *ftb, struct timeval now, const char *key, size_t keylen, long long weight, enum tb_consume_type cmd, long long tokens)
{
    if(weight>FAIR_TB_WEIGHT_MAX || weight<0)
        return -1;
    ST_hyperloglog_add(ftb->hll[weight-1], key, keylen, now);
    ftb_refill(ftb, now);

    int sfq_idx=0;
    sfq_idx=XXH3_64bits_withSeed(key, keylen, ftb->perturb_seed)%ftb->divisor;
    
    long long deficit_est=0;
    deficit_est=g_array_get(ftb->sfq, sfq_idx);
    deficit_est/=MAX(1, ftb->n_active_key/ftb->divisor);
    if(tokens + deficit_est > ftb->per_weight_quantum*weight)
    {
        return 0;
    }
    long long allocated_tokens=OC_token_bucket_consume(ftb->bucket, now, cmd, tokens);
    if(allocated_tokens) g_array_incrby(ftb->sfq, sfq_idx, allocated_tokens);
    return allocated_tokens;
}

void fair_token_bucket_info(const struct fair_token_bucket *ftb, struct timeval now, struct fair_token_bucket_info *info)
{
    info->active_key_number=ftb->n_active_key;
    info->divisor=ftb->divisor;
    OC_token_bucket_info(ftb->bucket, now, &info->bucket_info);
    return;
}
struct ftb_header
{
    long long magic;
    long long payload_sz;
    long long divisor;
    long long pertub_seed;
};
size_t fair_token_bucket_serialized_size(const struct fair_token_bucket *ftb)
{
    size_t sz=0;
    sz += sizeof(struct ftb_header);
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        sz += ST_hyperloglog_serialized_size(ftb->hll[i]);
    }
    sz += g_array_serialized_size(ftb->sfq);
    sz += OC_token_bucket_serialized_size(ftb->bucket);
    return sz;
}
void fair_token_bucket_serialize(const struct fair_token_bucket *ftb, char **blob, size_t *blob_sz)
{
    size_t sz=fair_token_bucket_serialized_size(ftb);
    size_t offset=0;
    struct ftb_header hdr;
    hdr.magic=0x5210;
    hdr.payload_sz=sz;
    hdr.divisor=ftb->divisor;
    hdr.pertub_seed=ftb->perturb_seed;

    char *buffer=ALLOC(char, sz);
    memcpy(buffer+offset, &hdr, sizeof(hdr));
	offset += sizeof(hdr);
    char *tmp_buff=NULL;
    size_t tmp_sz=0;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_serialize(ftb->hll[i], &tmp_buff, &tmp_sz);
        memcpy(buffer+offset, tmp_buff, tmp_sz);
        offset += ST_hyperloglog_serialized_size(ftb->hll[i]);
        free(tmp_buff);
    }
    g_array_serialize(ftb->sfq, &tmp_buff, &tmp_sz);
    memcpy(buffer+offset, tmp_buff, tmp_sz);
    assert(tmp_sz==g_array_serialized_size(ftb->sfq));
    offset += g_array_serialized_size(ftb->sfq);
    free(tmp_buff);
    OC_token_bucket_serialize(ftb->bucket, &tmp_buff, &tmp_sz);
    memcpy(buffer+offset, tmp_buff, tmp_sz);
    offset += OC_token_bucket_serialized_size(ftb->bucket);
    free(tmp_buff);
    *blob=buffer;
    *blob_sz=sz;
    assert(offset==sz);
    return;
}
struct fair_token_bucket *fair_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
    struct ftb_header hdr;
    size_t offset=0;
    memcpy(&hdr, blob+offset, sizeof(hdr));
    offset += sizeof(hdr);
    assert(hdr.magic==0x5210);
    assert(hdr.payload_sz<=blob_sz);
    struct fair_token_bucket *ftb=ALLOC(struct fair_token_bucket, 1);
    ftb->perturb_seed=hdr.pertub_seed;
    ftb->divisor=hdr.divisor;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ftb->hll[i]=ST_hyperloglog_deserialize(blob+offset, blob_sz-offset);
        offset += ST_hyperloglog_serialized_size(ftb->hll[i]);
    }
    ftb->sfq=g_array_deserialize(blob+offset, blob_sz-offset);
    offset += g_array_serialized_size(ftb->sfq);
    ftb->bucket=OC_token_bucket_deserialize(blob+offset, blob_sz-offset);
    offset += OC_token_bucket_serialized_size(ftb->bucket);
    assert(offset==hdr.payload_sz);
    return ftb;
}
void fair_token_bucket_merge(struct fair_token_bucket *dst, const struct fair_token_bucket *src)
{
    dst->perturb_seed=MAX(dst->perturb_seed, src->perturb_seed);
    dst->divisor=MAX(dst->divisor, src->divisor);
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_merge(dst->hll[i], src->hll[i]);
    }
    g_array_merge(dst->sfq, src->sfq);
    OC_token_bucket_merge(dst->bucket, src->bucket);
    return;
}
void fair_token_bucket_merge_blob(struct fair_token_bucket *ftb, const char *blob, size_t blob_sz)
{
    struct fair_token_bucket *src=fair_token_bucket_deserialize(blob, blob_sz);
    fair_token_bucket_merge(ftb, src);
    fair_token_bucket_free(src);
    return;
}
size_t fair_token_bucket_mem_size(const struct fair_token_bucket *ftb)
{
    size_t sz=0;
    sz += sizeof(struct fair_token_bucket);
    //to do
    return sz;
}