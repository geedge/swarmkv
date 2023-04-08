#include "fs_token_bucket.h"
#include "oc_token_bucket.h"
#include "cm_sketch.h"
#include "crdt_utils.h"

#include <math.h> //pow
#include <stdlib.h>//random
#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#define ROUND_INTERVAL_MS  100
#define MAX_CLASSES   128
#define EPOCH_INTERVAL_MS   100
struct fs_class
{
    long long class_id;
    long long weight;
    long long consumed;
    struct timeval last_round;
};
struct FS_token_bucket
{
    uuid_t uuid;
    size_t n_class;
    struct fs_class all_classes[MAX_CLASSES];
    int cms_idx;
    struct timeval last_deficit;
    long long per_weight_deficit;
    struct CM_sketch *consumed[2];
    long long CIR, CBS;
    long long total_weight;
    struct OC_token_bucket *bucket;
};

struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct FS_token_bucket *fstb=ALLOC(struct FS_token_bucket, 1);
    fstb->CIR=CIR;
    fstb->CBS=CBS;
    uuid_copy(fstb->uuid, uuid);
    fstb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);  
    return fstb;
}
void FS_token_bucket_free(struct FS_token_bucket *fstb)
{
    OC_token_bucket_free(fstb->bucket);
    free(fstb);
}

long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    long long got_tokens=0;
 
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
        self->consumed=0;
        memcpy(&self->last_round, &now, sizeof(self->last_round));
        fstb->n_class++;
        fstb->total_weight+=weight;
    }
    long long available=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_READ_AVAILABLE, 0);
    long long delta_time_ms=0;
    delta_time_ms=timeval_delta_ms(self->last_round, now);
    if(delta_time_ms>ROUND_INTERVAL_MS)
    {
        fstb->per_weight_deficit=available/fstb->total_weight;
        memcpy(&fstb->last_deficit, &now, sizeof(fstb->last_deficit));
    }
    delta_time_ms=timeval_delta_ms(self->last_round, now);
    
   // long long current_epoch=delta_time_ms/EPOCH_INTERVAL_MS;
    if(delta_time_ms>ROUND_INTERVAL_MS)
    {
        self->consumed=0;
        memcpy(&self->last_round, &now, sizeof(self->last_round));
    }
    //long long threshhold=available*self->weight/fstb->total_weight;
    if(tokens + self->consumed > fstb->per_weight_deficit*weight)
    {
        return 0;
    }
    got_tokens=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    self->consumed += got_tokens;
    return got_tokens;
}
