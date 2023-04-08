struct active_class
{
    long long class_id;
    long long weight;
    long long available_tokens;
};
struct HTB
{
    struct OR_map * map;//class_id -> struct active_class *
    struct PN_counter *root;
    struct timeval last_refill_time;
    long long CIR, CBS;
 //   struct OC_token_bucket *root;
};
void HTB_refill(struct HTB *htb, struct timeval now)
{
    struct OR_map_kv *list=NULL;
    size_t n=OR_map_get_all(htb->map, &list);
    struct active_class *class=NULL;
    long long delta_ms=timeval_delta_ms(htb->last_refill_time, now);
    long long generated_tokens=MIN(htb->CIR*delta_ms/1000, htb->CBS);
    long long left_tokens=generated_tokens;
    for(size_t i=0; i<n; i++)
    {
        class=(struct active_class *)list->string;
        //max-min fair share
        share=left_tokens/sum_weight;
        my_share=share*class->weight;
        if(class->available_tokens+ my_share< htb->CBS/(n+1))
        {
            class->available_tokens+=my_share;
        }
        left_tokens--;
        if(left_tokens==0) break;
    }
    PN_counter_incrby(htb->root, left_tokens);
    return;
}
long long HTB_consume(struct HTB *htb, struct timeval now, long long class_id, long long weight, long long tokens)
{
    struct active_class *class;

    class=OR_map_get(htb->map, class_id);
    if(tokens<class->available_tokens)
    {
        class->available_tokens-=tokens;
        return tokens;
    }
    else if(tokens<=PN_counter_get(htb->root))
    {
        PN_counter_incrby(htb->root, 0-tokens);
        return tokens;
    }
    else
    {
        return 0;
    }
}