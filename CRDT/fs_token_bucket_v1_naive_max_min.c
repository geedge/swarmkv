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

#define REFILL_INTERVAL_MS  100
#define MAX_CLASSES   128
struct fs_class
{
    long long class_id;
    long long weight;
    long long available;
};
struct FS_token_bucket
{
    uuid_t uuid;
    size_t n_class;
    struct fs_class all_classes[MAX_CLASSES];
    struct timeval last_refilled;
    long long CIR, CBS;
};

struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct FS_token_bucket *fstb=ALLOC(struct FS_token_bucket, 1);
    fstb->CIR=CIR;
    fstb->CBS=CBS;
    memcpy(&fstb->last_refilled, &now, sizeof(fstb->last_refilled));
    uuid_copy(fstb->uuid, uuid);
    return fstb;
}
void FS_token_bucket_free(struct FS_token_bucket *fstb)
{
    free(fstb);
}
static void max_min_refill(struct fs_class *classes, size_t n_class, long long per_class_CBS, long long tokens)
{
    long long total_weight=0;
    long long token_to_full=0;
    
    for(size_t i=0; i<n_class; i++)
    {
        total_weight+=classes[i].weight;
        token_to_full+=per_class_CBS-classes[i].available;
    }

    long long n_satisfied=0, left_tokens=tokens, left_weight=total_weight;
    while(n_satisfied<n_class && left_tokens/left_weight>0 )
    {
        long long share=left_tokens/left_weight;
        for(size_t i=0; i<n_class; i++)
        {
            long long my_share=classes[i].weight*share;
            if(classes[i].available == per_class_CBS)
            {
                continue;
            }
            else if(per_class_CBS - classes[i].available <= my_share)
            {
                left_tokens -= (per_class_CBS - classes[i].available);
                classes[i].available = per_class_CBS;
            }
            else
            {
                left_tokens -= my_share;
                classes[i].available += my_share;
                
            }
        }
        left_weight=0;
        n_satisfied=0;
        for(size_t i=0; i<n_class; i++)
        {
            if(classes[i].available == per_class_CBS)
            {
                n_satisfied++;
                continue;
            }
            left_weight += classes[i].weight;
            
        }
    }
}
long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    long long got_tokens=0;
    long long delta_time_ms=timeval_delta_ms(fstb->last_refilled, now);
    if(delta_time_ms>ROUND_INTERVAL_MS && fstb->n_class>0)
    {
        long long generated=fstb->CIR*delta_time_ms/1000;
        max_min_refill(fstb->all_classes, fstb->n_class, fstb->CBS/(fstb->n_class+1), generated);
        memcpy(&fstb->last_refilled, &now, sizeof(fstb->last_refilled));
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
        self->available=fstb->CBS/(fstb->n_class+1);
        fstb->n_class++;
    }
    if(tokens<=self->available)
    {
        self->available-=tokens;
        got_tokens=tokens;
    }
    return got_tokens;
}
