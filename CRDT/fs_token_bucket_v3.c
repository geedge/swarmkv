#include "fs_token_bucket.h"
#include "oc_token_bucket.h"
#include "st_hll.h"
#include "cm_sketch.h"
#include "crdt_utils.h"

#include <math.h> //pow
#include <stdlib.h>//random
#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#define REFILL_INTERVAL_MS  100
#define MAX_CLASSES   128
#define EPOCH_INTERVAL_MS   100
struct fs_class
{
    long long class_id;
    long long weight;
    long long deficit;
    struct timeval last_round;
};
struct FS_token_bucket
{
    uuid_t uuid;
    size_t n_class;
    struct fs_class all_classes[MAX_CLASSES];
    int cms_idx;
    struct timeval start_tv;
    struct timeval last_deficit;
    long long per_weight_share;

    long long CIR, CBS;
    long long total_weight;
    struct CM_sketch *tcms, *dcms;
    struct ST_HLL *hll; //count active classes
    struct OC_token_bucket *bucket;
};

struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct FS_token_bucket *fstb=ALLOC(struct FS_token_bucket, 1);
    fstb->CIR=CIR;
    fstb->CBS=CBS;
    uuid_copy(fstb->uuid, uuid);
    fstb->tcms=CM_sketch_new(uuid);
    fstb->dcms=CM_sketch_new(uuid);
    fstb->hll=ST_HLL_new(8, 10, &now);
    fstb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    memcpy(&fstb->start_tv, &now, sizeof(fstb->start_tv));
    return fstb;
}
void FS_token_bucket_free(struct FS_token_bucket *fstb)
{
    ST_HLL_free(fstb->hll);
    CM_sketch_free(fstb->tcms);
    CM_sketch_free(fstb->dcms);
    OC_token_bucket_free(fstb->bucket);
    free(fstb);
}

long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    long long got_tokens=0;
    long long deficit=0, refill_ts=0, now_ts=0;
    deficit=CM_sketch_query(fstb->dcms, (char *)&class_id, sizeof(class_id));
    refill_ts=CM_sketch_query(fstb->tcms, (char *)&class_id, sizeof(class_id));
    now_ts=timeval_delta_ms(fstb->start_tv, now);
    now_ts/=REFILL_INTERVAL_MS;
    if(now_ts>refill_ts)
    {
        CM_sketch_add_n(fstb->dcms, (char *)&class_id, sizeof(class_id), 0-deficit);
        CM_sketch_add_n(fstb->tcms, (char *)&class_id, sizeof(class_id), now_ts-refill_ts);
        deficit=0;
    }
    struct fs_class *self=NULL;
    for(size_t i=0; i<fstb->n_class; i++)
    {
        if(fstb->all_classes[i].class_id==class_id)
        {
            self=fstb->all_classes+i;
            break;
        }
    }
    if(!self)
    {
        self=fstb->all_classes + fstb->n_class;
        self->class_id=class_id;
        self->weight=weight;
        self->deficit=0;
        memcpy(&self->last_round, &now, sizeof(self->last_round));
        fstb->n_class++;
        fstb->total_weight+=weight;
    }
    
    long long delta_time_ms=0;
    delta_time_ms=timeval_delta_ms(fstb->last_deficit, now);
    if(delta_time_ms>REFILL_INTERVAL_MS)
    {
        long long available=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_READ_AVAILABLE, 0);
        fstb->per_weight_share=available/fstb->total_weight;
        memcpy(&fstb->last_deficit, &now, sizeof(fstb->last_deficit));
    }
    delta_time_ms=timeval_delta_ms(self->last_round, now);
    if(delta_time_ms > REFILL_INTERVAL_MS)
    {
        memcpy(&(self->last_round), &now, sizeof(self->last_round));
        self->deficit=0;
    }
    //deficit=self->deficit;
    if(tokens + deficit > fstb->per_weight_share*weight)
    {
        return 0;
    }
    got_tokens=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    CM_sketch_add_n(fstb->dcms, (char *)&class_id, sizeof(class_id), got_tokens);
    self->deficit += got_tokens;
    return got_tokens;
}
