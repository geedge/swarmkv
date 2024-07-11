#include "fair_token_bucket.h"
#include "oc_token_bucket.h"
#include "st_hyperloglog.h"
#include "crdt_utils.h"
#include "xxhash.h"

#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>


#define PERTUB_INTERVAL_MS  10
#define STHLL_WINDOW_MS 1000
//#define DEBUG

struct fair_token_bucket
{
    /* Sync Variables*/
    long long divisor;
    long long perturb_interval_ms;
    struct timeval last_cfg;
    struct ST_hyperloglog *hll[FAIR_TB_WEIGHT_MAX]; //counting active keys
    struct OC_token_bucket *bucket;
    
    /* Local Variables. Don't sync.*/
    long long perturb_seed;
    long long *sfq;  //per-key deficit estimation, size = divisor
    struct timeval last_pertub;
    long long per_weight_quantum; //updated every PERTUB_INTERVAL_MS
    long long total_weight;
    long long n_active_key;
    long long debug_last_defict;
    long long debug_last_allocated;
    long long debug_consume_call;
};
struct fair_token_bucket *fair_token_bucket_new(uuid_t uuid, struct timeval now, long long rate, long long period, long long capacity, long long divisor)
{
    struct fair_token_bucket *ftb=ALLOC(struct fair_token_bucket, 1);

    ftb->divisor=divisor;
    ftb->perturb_interval_ms=PERTUB_INTERVAL_MS;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ftb->hll[i]=ST_hyperloglog_new(7, STHLL_WINDOW_MS, now);
    }
    ftb->sfq=ALLOC(long long, divisor);
    ftb->bucket=OC_token_bucket_new(uuid, now, rate, period, capacity);
    ftb->per_weight_quantum=rate;
    memcpy(&ftb->last_cfg, &now, sizeof(ftb->last_cfg));
    return ftb;
}
void fair_token_bucket_set_pertub_interval(struct fair_token_bucket *ftb, long long perturb_interval_ms)
{
    ftb->perturb_interval_ms=perturb_interval_ms;
    return;
}
void fair_token_bucket_free(struct fair_token_bucket *ftb)
{
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_free(ftb->hll[i]);
    }
    free(ftb->sfq);
    OC_token_bucket_free(ftb->bucket);
    free(ftb);
}

static void ftb_quantum_estimation(struct fair_token_bucket *ftb, struct timeval now)
{
    //Per weight quantum estimation
    long long total_weight=0, n_active_key=0, count=0;
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_step(ftb->hll[i], now);//avoid STHLL deep copy 
        count = ST_hyperloglog_count(ftb->hll[i], now);
        total_weight += (i+1)*count;
        n_active_key += count;
    }
    assert(n_active_key>=0);
    struct OC_token_bucket_info info;
    OC_token_bucket_info(ftb->bucket, now, &info);
    

    long long local_available=tb_local_available(info.rate, info.period, info.available, info.capacity, info.number_of_replica);
    if(info.period==0)//Infinite Tokens
    {
        ftb->per_weight_quantum = INT64_MAX;
    }
    else
    {
        ftb->per_weight_quantum = local_available/MAX(1, total_weight);
    }
    ftb->total_weight=total_weight;
   
    ftb->n_active_key=n_active_key;
    return;
    if(ftb->perturb_seed==0)
        printf("seed\tavailable\tper_weight_quantum\tkey1\tkey2\n");
    printf("%lld\t%lld\t%lld\t", ftb->perturb_seed, local_available, ftb->per_weight_quantum);
    for(int i=0; i<ftb->divisor; i++)
    {
        if(ftb->sfq[i]>0)
            printf("%lld\t", ftb->sfq[i]);
    }
    printf("\n");
}
static void ftb_perturb(struct fair_token_bucket *ftb, struct timeval now)
{

    ftb_quantum_estimation(ftb, now);
    memset(ftb->sfq, 0, sizeof(long long)*ftb->divisor);
    ftb->perturb_seed++;
    memcpy(&ftb->last_pertub, &now, sizeof(ftb->last_pertub));
}
long long fair_token_bucket_consume(struct fair_token_bucket *ftb, struct timeval now, const char *key, size_t keylen, long long weight, enum tb_consume_type cmd, long long tokens)
{
    ftb->debug_consume_call++;
    if(weight>FAIR_TB_WEIGHT_MAX || weight<=0)
        return -1;

    ST_hyperloglog_add(ftb->hll[weight-1], key, keylen, now);

    long long delta_time_ms=timeval_delta_ms(ftb->last_pertub, now);
    if(unlikely(delta_time_ms>=ftb->perturb_interval_ms) )//|| ftb->consumed_of_current_perturb > ftb->consumed_of_last_perturb
    {
        ftb_perturb(ftb, now);
    }

    int sfq_idx=0;
    //sfq_idx=XXH3_64bits_withSeed(key, keylen, ftb->perturb_seed)%ftb->divisor;
    sfq_idx=XXH3_64bits_withSeed(key, keylen, 171)%ftb->divisor;
    
    long long deficit_est=0;
    deficit_est=ftb->sfq[sfq_idx];
    deficit_est/=MAX(1, ftb->n_active_key/ftb->divisor);

    ftb->debug_last_defict=deficit_est;
    if(cmd == TB_CONSUME_FLEXIBLE) tokens=MIN(tokens, MAX(0, ftb->per_weight_quantum*weight-deficit_est));
    if(tokens + deficit_est > ftb->per_weight_quantum*weight && cmd!=TB_CONSUME_FORCE)// && ftb->per_weight_quantum>0)
    {
        ftb->debug_last_allocated=-1;
        return 0;
    }
    long long allocated_tokens=OC_token_bucket_consume(ftb->bucket, now, cmd, tokens);
    if(allocated_tokens)
    {
        ftb->sfq[sfq_idx] += allocated_tokens;
    }
    return allocated_tokens;
}
void fair_token_bucket_configure(struct fair_token_bucket *ftb, struct timeval now, long long rate, long long period, long long capacity, long long divisor)
{
    ftb->divisor=divisor;
    ftb->sfq=(long long*)realloc(ftb->sfq, sizeof(long long)*ftb->divisor);
    memcpy(&ftb->last_cfg, &now, sizeof(ftb->last_cfg));
    OC_token_bucket_configure(ftb->bucket, now, rate, period, capacity);
    ftb_perturb(ftb, now);
    return;
}
void fair_token_bucket_info(const struct fair_token_bucket *ftb, struct timeval now, struct fair_token_bucket_info *info)
{
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        info->active_key_number += ST_hyperloglog_count(ftb->hll[i], now);
    }
    info->divisor = ftb->divisor;
    info->perturb_interval_ms = ftb->perturb_interval_ms;
    OC_token_bucket_info(ftb->bucket, now, &info->bucket_info);
    return;
}
struct ftb_header
{
    long long magic;
    long long payload_sz;
    long long divisor;
    long long perturb_interval_ms;
    struct timeval last_cfg;
};
size_t fair_token_bucket_serialized_size(const struct fair_token_bucket *ftb)
{
    size_t sz=0;
    sz += sizeof(struct ftb_header);
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        sz += ST_hyperloglog_serialized_size(ftb->hll[i]);
    }
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
    hdr.perturb_interval_ms=ftb->perturb_interval_ms;
    memcpy(&hdr.last_cfg, &ftb->last_cfg, sizeof(hdr.last_cfg));
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
    ftb->divisor = hdr.divisor;
    ftb->perturb_interval_ms = hdr.perturb_interval_ms;
    memcpy(&ftb->last_cfg, &hdr.last_cfg, sizeof(ftb->last_cfg));
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ftb->hll[i]=ST_hyperloglog_deserialize(blob+offset, blob_sz-offset);
        offset += ST_hyperloglog_serialized_size(ftb->hll[i]);
    }
    ftb->bucket=OC_token_bucket_deserialize(blob+offset, blob_sz-offset);
    offset += OC_token_bucket_serialized_size(ftb->bucket);
    assert(offset==hdr.payload_sz);
    return ftb;
}
void fair_token_bucket_merge(struct fair_token_bucket *dst, const struct fair_token_bucket *src)
{
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_merge(dst->hll[i], src->hll[i]);
    }
    if(timercmp(&dst->last_cfg, &src->last_cfg, <))//Last-Write-Wins
    {
        dst->divisor=src->divisor;
        dst->sfq=(long long*)realloc(dst->sfq, sizeof(long long)*dst->divisor);
        memcpy(&dst->last_cfg, &src->last_cfg, sizeof(dst->last_cfg));
    }
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
struct fair_token_bucket *fair_token_bucket_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
    struct fair_token_bucket *ftb=fair_token_bucket_deserialize(blob, blob_sz);
    OC_token_bucket_change_uuid(ftb->bucket, uuid);
    ftb->sfq=ALLOC(long long, ftb->divisor);
    return ftb;
}
size_t fair_token_bucket_mem_size(const struct fair_token_bucket *ftb)
{
    size_t sz=0;
    sz += sizeof(struct fair_token_bucket);
    for(int i=0; i<FAIR_TB_WEIGHT_MAX; i++)
    {
        sz += ST_hyperloglog_mem_size(ftb->hll[i]);
    }
    sz+=sizeof(long long)*ftb->divisor;
    sz+=OC_token_bucket_mem_size(ftb->bucket);
    return sz;
}