#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#include <stdint.h>

#define MAX_WEIGHT  20
#define SFTB_WIDTH  MAX_WEIGHT
#define SFTB_DEPTH  10
struct sf_token_bucket
{
    long long available_tokens[SFTB_WIDTH][SFTB_DEPTH];
    struct timeval last_refill;
    long long root_tokens;
    long long CIR, CBS;
};
long long sf_refill(struct sf_token_bucket *sftb, struct timeval now)
{
    long long delta_ms=0;
    long long generated_tokens=delta_ms*sftb->CIR/1000;
    long long left_tokens=generated_tokens;
    for(size_t i=0; i<SFTB_WIDTH; i++)
    {    
        for(size_t j=0; j<SFTB_DEPTH; j++)
        {
            long long added=MIN(generated_tokens/(SFTB_WIDTH*SFTB_DEPTH), sftb->CBS/(SFTB_WIDTH*SFTB_DEPTH)- sftb->available_tokens[i][j]);
            sftb->available_tokens[i][j]+=added;
            left_tokens-=added;
        }
    }
    sftb->root_tokens=MIN(sftb->root_tokens+left_tokens, sftb->CBS);
}
long long sf_consume(struct sf_token_bucket *sftb, long long class_id, long long weight, long long tokens)
{
    uint64_t hash[SFTB_WIDTH]={0};
    long long allocated=0, tmp=0;
    for(size_t i=0; i<weight; i++)
    {
        long long idx=hash[i];
        tmp=MAX(sftb->available_tokens[weight-i][idx], tokens-allocated/SFTB_DEPTH);
        sftb->available_tokens[weight-i][idx]-=tmp;
        allocated+=tmp;
        if(allocated==tokens) break;
    }
    if(allocated<tokens && sftb->root_tokens>tokens-allocated)
    {
            allocated=tokens;
            sftb->root_tokens -= (tokens-allocated);
    }
    return allocated;
}
