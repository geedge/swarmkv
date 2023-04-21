#include "fair_token_bucket.h"
#include "oc_token_bucket.h"
#include "st_hyperloglog.h"
#include "cm_sketch.h"
#include "crdt_utils.h"
#include "xxhash.h"

#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>


#define REFILL_INTERVAL_MS  200
#define STHLL_TIMEOUT_S    10
#define STHLL_PRECISION   8
#define CLASS_ID_MAX    8192
#define SFQ_BUCKET_MAX  1024
struct fair_token_bucket
{
    uuid_t uuid;
    long long CIR, CBS;

    struct timeval last_refill_time;
    long long per_weight_quantum; //updated every REFILL_INTERVAL_MS
    long long n_active_class;
    
    struct ST_hyperloglog *hll[FSTB_WEIGHT_MAX]; //counting active classes
    struct OC_token_bucket *bucket;
    long long deficit_array[CLASS_ID_MAX];//for debugging
    long long sfq[SFQ_BUCKET_MAX];
    long long n_class_per_weight[FSTB_WEIGHT_MAX];
    int perturb_seed;
};
struct fair_token_bucket *fair_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct fair_token_bucket *ftb=ALLOC(struct fair_token_bucket, 1);
    ftb->CIR=CIR;
    ftb->CBS=CBS;
    uuid_copy(ftb->uuid, uuid);
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        ftb->hll[i]=ST_hyperloglog_new(STHLL_PRECISION, STHLL_TIMEOUT_S, now);
    }
    ftb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    return ftb;
}
void fair_token_bucket_free(struct fair_token_bucket *fstb)
{
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        ST_hyperloglog_free(fstb->hll[i]);
    }
    OC_token_bucket_free(fstb->bucket);
    free(fstb);
}
static void ftb_refill(struct fair_token_bucket *fstb, struct timeval now)
{
    long long delta_time_ms=timeval_delta_ms(fstb->last_refill_time, now);
    if(likely(delta_time_ms<REFILL_INTERVAL_MS))
    {
        return;
    }
    long long total_weight=0, n_active_class=0, count=0;
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        count = ST_hyperloglog_count(fstb->hll[i]);
        total_weight += (i+1)*count;
        n_active_class+=count;
        fstb->n_class_per_weight[i]=count;
    }
    struct OC_token_bucket_info info;
    OC_token_bucket_info(fstb->bucket, now, &info);
    long long available=info.available;     
    fstb->per_weight_quantum=available/total_weight;
    fstb->n_active_class=n_active_class;
    
    memcpy(&fstb->last_refill_time, &now, sizeof(fstb->last_refill_time));
    memset(fstb->deficit_array, 0, sizeof(fstb->deficit_array));
    memset(fstb->sfq, 0, sizeof(fstb->sfq));
    fstb->perturb_seed++;
}
long long fair_token_bucket_consume(struct fair_token_bucket *ftb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    assert(weight<=FSTB_WEIGHT_MAX && weight>0);
    assert(class_id<CLASS_ID_MAX);
    ST_hyperloglog_add(ftb->hll[weight-1], (char *)&class_id, sizeof(class_id), now);
    ftb_refill(ftb, now);
    long long deficit_exact=0, deficit_est=0;
    //deficit_est=CM_sketch_query(fstb->cms_deficit, (char *)&class_id, sizeof(class_id));
    deficit_exact=ftb->deficit_array[class_id];
    int sfq_idx=0;
    sfq_idx=XXH3_64bits_withSeed((char *)&class_id, sizeof(class_id), ftb->perturb_seed)%SFQ_BUCKET_MAX;
    deficit_est=ftb->sfq[sfq_idx];
    deficit_est/=MAX(1, ftb->n_active_class/SFQ_BUCKET_MAX);
    if(tokens + deficit_est > ftb->per_weight_quantum*weight)
    {
        return 0;
    }
    long long got_tokens=OC_token_bucket_consume(ftb->bucket, now, TB_CMD_CONSUME_NORMAL, tokens);
    //CM_sketch_add_n(fstb->cms_deficit, (char *)&class_id, sizeof(class_id), got_tokens);
    ftb->deficit_array[class_id] += got_tokens;
    ftb->sfq[sfq_idx] += got_tokens;
    return got_tokens;
}

void fair_token_bucket_info(const struct fair_token_bucket *fstb, struct timeval now, struct fair_token_bucket_info *info)
{
    info->active_classes_number=fstb->n_active_class;
    OC_token_bucket_info(fstb->bucket, now, &info->bucket_info);
    return;
}