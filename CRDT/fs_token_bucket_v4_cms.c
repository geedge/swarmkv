#include "fs_token_bucket.h"
#include "oc_token_bucket.h"
#include "st_hll.h"
#include "cm_sketch.h"
#include "crdt_utils.h"
#include "xxhash.h"

#include <math.h> //pow
#include <stdlib.h>//random
#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>


#define REFILL_INTERVAL_MS  100
#define STHLL_TIMEOUT_S    10
#define STHLL_PRECISION   8

struct FS_token_bucket
{
    uuid_t uuid;
    long long CIR, CBS;

    struct timeval last_refill_time;
    long long per_weight_quantum; //updated every REFILL_INTERVAL_MS
    long long n_active_class;
    
    struct ST_HLL *hll[FSTB_WEIGHT_MAX]; //counting active classes
    struct CM_sketch *cms_deficit;
    struct OC_token_bucket *bucket;
};
struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct FS_token_bucket *fstb=ALLOC(struct FS_token_bucket, 1);
    fstb->CIR=CIR;
    fstb->CBS=CBS;
    uuid_copy(fstb->uuid, uuid);
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        fstb->hll[i]=ST_HLL_new(STHLL_PRECISION, STHLL_TIMEOUT_S, &now);
    }
    fstb->cms_deficit=CM_sketch_new(uuid);
    fstb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    return fstb;
}
void FS_token_bucket_free(struct FS_token_bucket *fstb)
{
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        ST_HLL_free(fstb->hll[i]);
    }
    CM_sketch_free(fstb->cms_deficit);
    OC_token_bucket_free(fstb->bucket);
    free(fstb);
}
static void fstb_refill(struct FS_token_bucket *fstb, struct timeval now)
{
    long long delta_time_ms=timeval_delta_ms(fstb->last_refill_time, now);
    if(likely(delta_time_ms<REFILL_INTERVAL_MS))
    {
        return;
    }
    long long total_weight=0, n_active_class=0, count=0;
    for(int i=0; i<FSTB_WEIGHT_MAX; i++)
    {
        count = ST_HLL_count(fstb->hll[i]);
        total_weight += (i+1)*count;
        n_active_class+=count;

    }
    long long available=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_READ_AVAILABLE, 0);        
    fstb->per_weight_quantum=available/total_weight;
    fstb->n_active_class=n_active_class;
    
    CM_sketch_free(fstb->cms_deficit);
    fstb->cms_deficit=CM_sketch_new(fstb->uuid);
    memcpy(&fstb->last_refill_time, &now, sizeof(fstb->last_refill_time));
}
long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    assert(weight<=FSTB_WEIGHT_MAX && weight>0);
    ST_HLL_add(fstb->hll[weight-1], (char *)&class_id, sizeof(class_id), &now);
    fstb_refill(fstb, now);
    long long deficit=0;
    deficit=CM_sketch_query(fstb->cms_deficit, (char *)&class_id, sizeof(class_id));
    if(tokens + deficit > fstb->per_weight_quantum*weight)
    {
        return 0;
    }
    long long got_tokens=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    CM_sketch_add_n(fstb->cms_deficit, (char *)&class_id, sizeof(class_id), got_tokens);
    return got_tokens;
}

void FS_token_bucket_info(const struct FS_token_bucket *fstb, struct FS_token_bucket_info *info)
{
    info->active_classes_number=fstb->n_active_class;
    OC_token_bucket_info(fstb->bucket, &info->bucket_info);
    return;
}