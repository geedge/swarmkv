#include "sf_token_bucket.h"
#include "oc_token_bucket.h"
#include "cm_sketch.h"
#include "crdt_utils.h"

#include <math.h> //pow
#include <stdlib.h>//random
#include <string.h> 
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#define SFTB_REFRESH_INTERVAL_MS 20
#define SFTB_WARMUP_INTERVAL_MS  500
#define SFTB_MAX_WEIGHT  20
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
    struct CM_sketch *ucms[2];
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
    struct SF_token_bucket *sftb=ALLOC(struct SF_token_bucket, 1);
    sftb->bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
    sftb->CIR=CIR;
    sftb->cms[0]=CM_sketch_new(uuid);
    sftb->cms[1]=CM_sketch_new(uuid);
    sftb->ucms[0]=CM_sketch_new(uuid);
    sftb->ucms[1]=CM_sketch_new(uuid);
    memcpy(&sftb->refresh_time, &now, sizeof(sftb->refresh_time));
    uuid_copy(sftb->uuid, uuid);
    return sftb;
}
void SF_token_bucket_free(struct SF_token_bucket *sftb)
{
    OC_token_bucket_free(sftb->bucket);
    CM_sketch_free(sftb->cms[0]);
    CM_sketch_free(sftb->cms[1]);
    CM_sketch_free(sftb->ucms[0]);
    CM_sketch_free(sftb->ucms[1]);
    free(sftb);
}

static double sft_prob(const struct CM_sketch *cms, long long class_id)
{
    long long class_consumed=0, total_consumed=0, min=0, max=0;
    class_consumed=CM_sketch_query(cms, (char*) &class_id, sizeof(class_id));
    total_consumed=CM_sketch_elements(cms);
    assert(total_consumed<__INT32_MAX__);
    min=CM_sketch_min(cms);
    max=CM_sketch_max(cms);
    if(min==max || total_consumed==0||min==0)
    {
        return 1;
    }
    double p_consumed=(double)total_consumed/class_consumed;
    double p_max=(double)total_consumed/min;
    double p_min=(double)total_consumed/max;

    double p_pass=(p_consumed-p_min)/(p_max-p_min);
    //pow(1-p_consumed, 2); 
    return p_pass;
}

static double sft_prob2(const struct CM_sketch *cms, long long class_id)
{
    long long unresponse_cnt=0, total=0, min=0, max=0;
    unresponse_cnt=CM_sketch_query(cms, (char*) &class_id, sizeof(class_id));
    total=CM_sketch_elements(cms);
    assert(total<__INT32_MAX__);
    min=CM_sketch_min(cms);
    max=CM_sketch_max(cms);
    if(min==max || total==0)
    {
        return 1;
    }
    double p_self=(double)unresponse_cnt/total;
    double p_max=(double)max/total;
    double p_min=(double)min/total;

    double p_pass=1-(p_self-p_min)/(p_max-p_min);
    return p_pass;
}
long long SF_token_bucket_consume(struct SF_token_bucket *sftb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    long long got_tokens=0;
    int r=0;
    long long delta_time_ms=timeval_delta_ms(sftb->refresh_time, now);
    if(delta_time_ms>SFTB_REFRESH_INTERVAL_MS)
    {
        sftb->n_request=0;
        sftb->n_jam=0;
        CM_sketch_free(sftb->cms[sftb->cms_idx]);
        sftb->cms[sftb->cms_idx]=CM_sketch_new(sftb->uuid);
        CM_sketch_free(sftb->ucms[sftb->cms_idx]);
        sftb->ucms[sftb->cms_idx]=CM_sketch_new(sftb->uuid);
        sftb->cms_idx^=1;
        memcpy(&sftb->refresh_time, &now, sizeof(sftb->refresh_time));
    }
    long long available=OC_token_bucket_control(sftb->bucket, now, OCTB_CMD_READ_AVAILABLE, 0);
    long long total=sftb->CIR*SFTB_REFRESH_INTERVAL_MS/1000;
//    long long my_consumed=CM_sketch_query(sftb->cms[sftb->cms_idx], (char*) &class_id, sizeof(class_id));
    double p_avail=(double)available/total;
    //if(sftb->n_jam)
    //if((double)sftb->n_jam*weight/sftb->n_request>0.001)
    //if((double)sftb->n_jam/sftb->n_request>0.1)
    if(p_avail<(double)1/weight/2.1) //current BEST
    //if(my_consumed/total>0.2)
    //if(p_avail<0.1)
    //if((sftb->n_jam>weight*20))
    //if((double)available/(sftb->CIR*SFTB_REFRESH_INTERVAL_MS/1000) < 0.9)
    //long long failed_cnt=CM_sketch_query(sftb->ucms[sftb->cms_idx],  (char *)&class_id, sizeof(class_id));
    //if(failed_cnt>4)

    //long long allocated_tokens=CM_sketch_query(sftb->cms[sftb->cms_idx],  (char *)&class_id, sizeof(class_id));
    //long long rejected_tokens=CM_sketch_query(sftb->ucms[sftb->cms_idx],  (char *)&class_id, sizeof(class_id));
    //if((double)rejected_tokens/allocated_tokens>0.2)
    {
        double p_pass1=sft_prob(sftb->cms[sftb->cms_idx], class_id);
        double p_pass2=sft_prob(sftb->ucms[sftb->cms_idx], class_id);
        //double p_pass=sqrt(pow(p_pass1, 2) + pow(p_pass2, 2))/2;
        
        r=random()%1000;
        if(r>p_pass2*1000)
        {
            return 0;
        }
    }
    got_tokens=OC_token_bucket_control(sftb->bucket, now, OCTB_CMD_CONSUME_NORMAL, tokens);
    if(got_tokens==0 && tokens>0)
    {
        CM_sketch_add_n(sftb->ucms[sftb->cms_idx], (char *)&class_id, sizeof(class_id), tokens/weight);
        CM_sketch_add_n(sftb->ucms[sftb->cms_idx^1], (char *)&class_id, sizeof(class_id), tokens/weight);
        sftb->n_jam++;
    }
    else
    {
        CM_sketch_add_n(sftb->cms[sftb->cms_idx], (char *)&class_id, sizeof(class_id), tokens/weight);
        CM_sketch_add_n(sftb->cms[sftb->cms_idx^1], (char *)&class_id, sizeof(class_id), tokens/weight);
    }
    sftb->n_request++;
    return got_tokens;
}
double SF_token_bucket_get_prob(struct SF_token_bucket *sftb, struct timeval now, long long class_id)
{

    double p_pass1=sft_prob(sftb->cms[sftb->cms_idx], class_id);
    double p_pass2=sft_prob(sftb->ucms[sftb->cms_idx], class_id);
    double p_pass=(p_pass1+p_pass2)/2;

    //p_pass=sqrt(pow(p_pass, 2)+pow(p_avail, 2));
    return p_pass2;
}
