#include "sf_token_bucket.h"
#include "oc_token_bucket.h"
#include "cm_sketch.h"
#include "crdt_utils.h"

#include <math.h> //pow
#include <stdlib.h>//random
#include <string.h> 
#include <assert.h>
#include <stdio.h>

#define SFT_REFRESH_INTERVAL_MS 2000
#define SFT_WARMUP_INTERVAL_MS  500
struct SF_token_bucket
{
    uuid_t uuid;
    int type; //fairness or split by
    long long CIR;
    struct OC_token_bucket *bucket;
    int cms_idx;   //current active cms (0 or 1)
    char double_buffering;
    int n_request;
    int n_jam;
    struct CM_sketch *cms[2];   //two cms for double buffer
    struct timeval refresh_time;//timestamp of last cms changing
};
int stochastic_pass(double pc, double pa)
{
    double p=0.0;
    p=pow((1-pa), 2)+pow(1-pc, 2);
	p=sqrt(p);
    int r=random()%65536;
    return r<65536*p?1:0;
}
struct SF_token_bucket *SF_token_bucket_new(uuid_t uuid, struct timeval now, int type, long long CIR, long long CBS)
{
    struct SF_token_bucket *sft=ALLOC(struct SF_token_bucket, 1);
    sft->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    sft->CIR=CIR;
    sft->cms[0]=CM_sketch_new(uuid);
    sft->cms[1]=CM_sketch_new(uuid);
    memcpy(&sft->refresh_time, &now, sizeof(sft->refresh_time));
    return sft;
}
void SF_token_bucket_free(struct SF_token_bucket *sft)
{
    OC_token_bucket_free(sft->bucket);
    CM_sketch_free(sft->cms[0]);
    CM_sketch_free(sft->cms[1]);
    free(sft);
}
static double sft_prob(const struct CM_sketch *cms, long long class_id)
{
    long long class_consumed=0, total_consumed=0, min=0, max=0;
    class_consumed=CM_sketch_query(cms, (char*) &class_id, sizeof(class_id));
    total_consumed=CM_sketch_elements(cms);
    assert(total_consumed<__INT32_MAX__);
    min=CM_sketch_min(cms);
    max=CM_sketch_max(cms);
    double p_consumed=(double)total_consumed/class_consumed;
    double p_max=(double)total_consumed/min;
    double p_min=(double)total_consumed/max;
    if(p_max==p_min)
    {
        return 1;
    }
    double p_pass=(p_consumed-p_min)/(p_max-p_min);
    //pow(1-p_consumed, 2); 
    return p_pass;
}
long long SF_token_bucket_consume(struct SF_token_bucket *sft, struct timeval now, long long class_id, long long weight, long long tokens)
{
    long long got_tokens=0;
    int r=0;
    long long delta_time_ms=timeval_delta_ms(sft->refresh_time, now);
    if(delta_time_ms>SFT_REFRESH_INTERVAL_MS)
    {
        sft->n_request=0;
        sft->n_jam=0;
        CM_sketch_free(sft->cms[sft->cms_idx]);
        sft->cms[sft->cms_idx]=CM_sketch_new(sft->uuid);
        sft->cms_idx^=1;
        memcpy(&sft->refresh_time, &now, sizeof(sft->refresh_time));
    }
    if((double)sft->n_jam/sft->n_request>0.01)
    {
        double p_pass=sft_prob(sft->cms[sft->cms_idx], class_id);
        r=random()%1000;
        if(r>p_pass*1000)
        {
            return 0;
        }
    }
    got_tokens=OC_token_bucket_control(sft->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    if(got_tokens)
    {
        CM_sketch_add_n(sft->cms[sft->cms_idx], (char *)&class_id, sizeof(class_id), got_tokens/weight);
        CM_sketch_add_n(sft->cms[sft->cms_idx^1], (char *)&class_id, sizeof(class_id), got_tokens/weight);
    }
    if(got_tokens==0 && tokens>0)
    {
        sft->n_jam++;
    }
    sft->n_request++;
    return got_tokens;
}
void SF_token_bucket_print_prob(struct SF_token_bucket *sft, long long *classes, size_t n_class)
{
    for(size_t i=0; i<n_class; i++)
    {
        printf("class=%lld prob=%f\r\n", classes[i], sft_prob(sft->cms[sft->cms_idx], classes[i]));
    }
}