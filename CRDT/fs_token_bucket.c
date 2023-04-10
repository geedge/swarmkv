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

#define MAX_WEIGHT          20
#define REFILL_INTERVAL_MS  100
#define DEFICIT_TABLE_SIZE  1024
struct deficit_table
{
    struct timeval reset_time;
    long long deficit[DEFICIT_TABLE_SIZE];
};
struct FS_token_bucket
{
    uuid_t uuid;
    long long CIR, CBS;

    struct deficit_table table;
    struct timeval last_refill_time;
    long long per_weight_share; //updated every REFILL_INTERVAL_MS
    long long n_active_class;
    
    struct ST_HLL *hll[MAX_WEIGHT]; //counting active classes
    struct CM_sketch *cms_deficit;
    struct OC_token_bucket *bucket;
};
struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS)
{
    struct FS_token_bucket *fstb=ALLOC(struct FS_token_bucket, 1);
    fstb->CIR=CIR;
    fstb->CBS=CBS;
    uuid_copy(fstb->uuid, uuid);
    for(int i=0; i<MAX_WEIGHT; i++)
    {
        fstb->hll[i]=ST_HLL_new(8, 10, &now);
    }
    fstb->cms_deficit=CM_sketch_new(uuid);
    fstb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    return fstb;
}
void FS_token_bucket_free(struct FS_token_bucket *fstb)
{
    for(int i=0; i<MAX_WEIGHT; i++)
    {
        ST_HLL_free(fstb->hll[i]);
    }
    CM_sketch_free(fstb->cms_deficit);
    OC_token_bucket_free(fstb->bucket);
    free(fstb);
}
static void fstb_refill(struct FS_token_bucket *fstb, struct timeval now)
{
    long long delta_time_ms=timeval_delta_ms(fstb->table.reset_time, now);
    if(delta_time_ms<REFILL_INTERVAL_MS)
    {
        return;
    }
    long long total_weight=0;
    for(int i=0; i<MAX_WEIGHT; i++)
    {
        total_weight += (i+1)*ST_HLL_count(fstb->hll[i]);
    }
    long long available=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_READ_AVAILABLE, 0);        
    fstb->per_weight_share=available/total_weight;
    memset(fstb->table.deficit, 0, sizeof(fstb->table.deficit));
    CM_sketch_free(fstb->cms_deficit);
    fstb->cms_deficit=CM_sketch_new(fstb->uuid);
    memcpy(&fstb->table.reset_time, &now, sizeof(fstb->table.reset_time));
}
long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    assert(weight<=MAX_WEIGHT && weight>0);
    ST_HLL_add(fstb->hll[weight-1], (char *)&class_id, sizeof(class_id), &now);
    fstb_refill(fstb, now);
    int idx=XXH32(&class_id, sizeof(class_id), 0)%DEFICIT_TABLE_SIZE;
    long long deficit=fstb->table.deficit[idx]/MAX(1, fstb->n_active_class/DEFICIT_TABLE_SIZE);
    deficit=CM_sketch_query(fstb->cms_deficit, (char *)&class_id, sizeof(class_id));
    if(tokens + deficit > fstb->per_weight_share*weight)
    {
        return 0;
    }
    long long got_tokens=OC_token_bucket_control(fstb->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    CM_sketch_add_n(fstb->cms_deficit, (char *)&class_id, sizeof(class_id), got_tokens);
    fstb->table.deficit[idx] += got_tokens;
    return got_tokens;
}