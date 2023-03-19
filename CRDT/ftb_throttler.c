#include "ftb_throttler.h"
#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"
#include "xxhash.h"

#include <assert.h>
#include <string.h>

#define MAX_SKETCH_DEPTH 4
#define MAX_SKETCH_WIDTH 1024
#define MAX_QUEUE_DEPTH 2000

struct ftbt_class_id
{
    unsigned long class_hash[MAX_SKETCH_DEPTH];
    unsigned char weight;
};

struct active_class_queue
{
    struct ftbt_class_id queue[MAX_QUEUE_DEPTH];
    long long head;
    long long tail;
    long long len;
    long long alloc_tokens; 
};

struct count_min_sketch
{
    long long *occupied_token[MAX_SKETCH_DEPTH];
    long long perturbation[MAX_SKETCH_DEPTH];
    size_t depth;
    size_t width;
};

struct ftbt_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_duration_ms;
	struct timeval write_timestamp;
};
struct ftb_throttler
{
	struct ftbt_configuration cfg;
	long long refilled;
	struct timeval refill_timestamp;
	struct PN_counter *consumed;
    struct count_min_sketch sketch;
    struct active_class_queue active_q;
};

static void ftbt_perturbation(struct ftb_throttler *t)
{
    size_t i;
    srand((unsigned)time(NULL));
    for(i = 0; i < t->sketch.depth; ++i)
    {
        t->sketch.perturbation[i] = rand();
    }
}

static void ftbt_fill_class_id(struct ftbt_class_id *cid, struct ftb_throttler *t, unsigned long req_class_id, unsigned char weight)
{
    unsigned long *p;
    for(size_t i = 0; i < t->sketch.depth; ++i)
    {
        p = &req_class_id;
        cid->class_hash[i] = XXH64(p, sizeof(req_class_id), t->sketch.perturbation[i]);;
    }
    cid->weight = weight;
}

static void ftbt_update_sketch(struct count_min_sketch *sketch, unsigned long *hash, long long count)
{
    size_t i = 0;
    unsigned long hashval = 0;
    for(i = 0; i < sketch->depth; ++i)
    {
        hashval = hash[i] % sketch->width;
        sketch->occupied_token[i][hashval] += count;
    }
}
static long long ftbt_estimate_sketch(struct count_min_sketch sketch, unsigned long *hash)
{
    size_t i = 0;
    long long minval = LLONG_MAX;
    long long hashval = 0;
    for(i = 0; i < sketch.depth; ++i)
    {
        hashval = hash[i] % sketch.width;
        minval = MIN(minval, sketch.occupied_token[i][hashval]);
    }
    return minval;
}

static int ftbt_aq_enqueue(struct ftbt_class_id *cid, struct ftb_throttler *t)
{
    struct active_class_queue *aq = &t->active_q;
    if(aq->len >= MAX_QUEUE_DEPTH)
    {
        printf("Unexpected: active flow queue full.\n");
        return FALSE;
    }
    memcpy(&(aq->queue[aq->tail]), cid, sizeof(struct ftbt_class_id));
    aq->tail = (aq->tail + 1) % MAX_QUEUE_DEPTH;
    aq->len++;
    return TRUE;
}

//update weight
static void ftbt_operate_weight(struct ftbt_class_id cid, struct ftb_throttler *t)
{
    struct active_class_queue *aq =  &t->active_q;
    size_t i,j;
    for(i = aq->head; i != aq->tail; i = (i+1)%MAX_QUEUE_DEPTH)
    {
        for(j = 0; j < t->sketch.depth; ++j)
        {
            if(aq->queue[i].class_hash[j] != cid.class_hash[j])
                break;
        }
        if(j == t->sketch.depth)
            break;
    }
    aq->queue[i].weight = cid.weight;
}

static void ftbt_aq_dequeue(struct ftb_throttler *t, long long available)
{
    struct active_class_queue *aq = &t->active_q;
    long long occupied_tokens = 0;
    if(available > 0)
    {
        while (aq->len > 0)
        {
            struct ftbt_class_id *cid = &aq->queue[aq->head];
            occupied_tokens = ftbt_estimate_sketch(t->sketch, cid->class_hash);
            unsigned char weight = cid->weight;
            long long alloc_tokens_weighted = aq->alloc_tokens * weight;
            long long consume = occupied_tokens > alloc_tokens_weighted ? alloc_tokens_weighted : occupied_tokens;
            if(available - consume >= 0)
            {
                ftbt_update_sketch(&t->sketch, cid->class_hash, -consume);
                if(occupied_tokens > alloc_tokens_weighted)
                    ftbt_aq_enqueue(cid, t);
                PN_counter_incrby(t->consumed, consume);
                available -= consume;
            }else{
                break;
            }
            aq->head = (aq->head + 1) % MAX_QUEUE_DEPTH;
            aq->len--;
        }
    }
}

struct ftb_throttler *ftb_throttler_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long alloc_tokens)
{
	struct ftb_throttler *t=ALLOC(struct ftb_throttler, 1);
	t->consumed=PN_counter_new(my_id);
	gettimeofday(&t->cfg.write_timestamp, NULL);
	memcpy(&t->cfg.write_timestamp, &now, sizeof(t->cfg.write_timestamp));
	t->cfg.CIR=CIR;
	t->cfg.CBS=CBS;
	t->cfg.refill_duration_ms=1;
	memcpy(&t->refill_timestamp, &now, sizeof(t->cfg.write_timestamp));
	t->refilled=t->cfg.CBS;
    //active queue
    t->active_q.alloc_tokens = alloc_tokens/1000; //可调整
    //sketch
    t->sketch.depth = MAX_SKETCH_DEPTH;
    t->sketch.width = MAX_SKETCH_WIDTH;
    for(size_t i = 0; i < t->sketch.depth; ++i){
        t->sketch.occupied_token[i] = ALLOC(long long, t->sketch.width);
    }
    ftbt_perturbation(t);
	return t;
}

void ftb_throttler_free(struct ftb_throttler *t)
{
	PN_counter_free(t->consumed);
    for(size_t i = 0; i < t->sketch.depth; ++i){
        free(t->sketch.occupied_token[i]);
    }
	free(t);
}

void ftb_throttler_configure(struct ftb_throttler *t, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms)
{
	memcpy(&t->cfg.write_timestamp, &now, sizeof(t->cfg.write_timestamp));
	if(CIR>=0) t->cfg.CIR=CIR;
	if(CBS>=0) t->cfg.CBS=CBS;
	if(refill_duration_ms>0) t->cfg.refill_duration_ms=refill_duration_ms;
}

long long ftb_throttler_control(struct ftb_throttler *t, struct timeval now, enum ftb_throttler_command cmd, long long tokens, unsigned long req_class_id, unsigned char weight)
{
	long long delta_time_ms=timeval_delta_ms(t->refill_timestamp, now);
	long long consumed=PN_counter_get(t->consumed);
	long long to_add=0;
	long long refilled=t->refilled;
	assert(refilled>=0);
	assert(consumed>=0);
	int refill_flag=0;
    long long global_available, local_available;
    struct ftbt_class_id cid;
    long long occupied_tokens, token_threshold;
    int rtn = 0;

    //dequeue
    global_available=MAX(refilled-consumed, 0);
    local_available = global_available; //not reserved
    if(delta_time_ms >= t->cfg.refill_duration_ms)
    {
        ftbt_aq_dequeue(t, local_available);
    }
    consumed = PN_counter_get(t->consumed);
    refilled = t->refilled;

    if(t->cfg.CBS==0 && t->cfg.CIR==0)
    {
		refilled+=tokens;
		refill_flag=1;
    }

    to_add=t->cfg.CIR*delta_time_ms/1000;
	if(delta_time_ms>=t->cfg.refill_duration_ms && to_add>0)
	{
		if(refilled<consumed)
		{
			refilled=consumed;
		}
		if(to_add + refilled - consumed < t->cfg.CBS)
		{
			refilled+=to_add;
		}
		else
		{
			refilled+=t->cfg.CBS - (refilled - consumed);
		}
		refill_flag=1;
	}

	global_available=MAX(refilled-consumed, 0);
	local_available=global_available;
    ftbt_fill_class_id(&cid, t, req_class_id, weight);
	ftbt_operate_weight(cid, t);
    occupied_tokens = ftbt_estimate_sketch(t->sketch, cid.class_hash);
    token_threshold = local_available*weight;

	switch(cmd)
	{
		case FTBT_CMD_CONSUME_FLEXIBLE:
            if (occupied_tokens <= token_threshold)
            {
                if (occupied_tokens == 0)
                    ftbt_aq_enqueue(&cid, t); 
                ftbt_update_sketch(&t->sketch, cid.class_hash, tokens);
                rtn++;
            }
			break;
		case FTBT_CMD_CONSUME_NORMAL:
			if (occupied_tokens <= token_threshold && local_available >= tokens)
            {
                if (occupied_tokens == 0)
                    ftbt_aq_enqueue(&cid, t); 
                ftbt_update_sketch(&t->sketch, cid.class_hash, tokens);
                rtn++;
            }
			break;
		default:
			assert(0);
			break;
	}
	if(refill_flag)
	{
		t->refilled=refilled;		
		memcpy(&t->refill_timestamp, &now, sizeof(t->refill_timestamp));
	}

	return rtn;
}
long long ftbt_throttler_read_refilled(struct ftb_throttler *t)
{
    return t->refilled;
}
long long ftbt_throttler_read_available(struct ftb_throttler *t)
{
	long long consumed=PN_counter_get(t->consumed);
	long long global_available=MAX(t->refilled-consumed, 0);
    return global_available;
}
void ftb_throttler_info(struct ftb_throttler *t, struct ftb_throttler_info *info)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	info->CIR=t->cfg.CIR;
	info->CBS=t->cfg.CBS;
	info->refill_duration_ms=t->cfg.refill_duration_ms;
	info->consumed=PN_counter_get(t->consumed);
	info->refilled=t->refilled;
	info->available=ftbt_throttler_read_available(t);
	return;
}

void ftb_throttler_serialize(struct ftb_throttler *t, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
    char *mpack_buff=NULL;
    size_t mpack_sz=0;

    char *value_blob=NULL;
    size_t value_blob_sz=0;

    mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);
    mpack_build_array(&writer);

    PN_counter_serialize(t->consumed, &value_blob, &value_blob_sz);
    mpack_write_bin(&writer, value_blob, value_blob_sz);
    free(value_blob);


    mpack_write_i64(&writer, t->refilled);

    mpack_write_bin(&writer, (const char*)&(t->refill_timestamp), sizeof(t->refill_timestamp));
    mpack_write_i64(&writer, t->cfg.CIR);
    mpack_write_i64(&writer, t->cfg.CBS);

    mpack_complete_array(&writer);
    mpack_writer_destroy(&writer);

    *blob=mpack_buff;
    *blob_sz=mpack_sz;
	return;
}
struct ftb_throttler *ftb_throttler_deserialize(const char *blob, size_t blob_sz)
{
	struct ftb_throttler *t=ALLOC(struct ftb_throttler, 1);
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, blob_sz);
    mpack_tree_parse(&tree);
    mpack_node_t root=mpack_tree_root(&tree);

    size_t i=0;
    mpack_node_t tmp_node;

    tmp_node=mpack_node_array_at(root, i++);
    t->consumed = PN_counter_deserialize(mpack_node_bin_data(tmp_node), mpack_node_bin_size(tmp_node));

    tmp_node=mpack_node_array_at(root, i++);
    t->refilled = mpack_node_i64(tmp_node);

    tmp_node=mpack_node_array_at(root, i++);
    assert(mpack_node_bin_size(tmp_node)==sizeof(t->refill_timestamp));
    memcpy(&t->refill_timestamp, mpack_node_bin_data(tmp_node), mpack_node_bin_size(tmp_node));

    tmp_node=mpack_node_array_at(root, i++);
    t->cfg.CIR = mpack_node_i64(tmp_node);

    tmp_node=mpack_node_array_at(root, i++);
    t->cfg.CBS = mpack_node_i64(tmp_node);

    if (mpack_tree_destroy(&tree) != mpack_ok)
    {
        fprintf(stderr, "An error occurred decoding the ftbf_throttle!\n");
    }
	return t;
}
void ftb_throttler_merge(struct ftb_throttler *src, struct ftb_throttler *dst)
{
    PN_counter_merge(src->consumed, dst->consumed);
    if(src->refilled > dst->refilled)
    {
        dst->refilled = src->refilled;
        dst->refill_timestamp = src->refill_timestamp;
    }
    else
    {
        src->refilled = dst->refilled;
        src->refill_timestamp = dst->refill_timestamp;
    }
}
void ftb_throttler_merge_blob(struct ftb_throttler *t, const char *blob, size_t blob_sz)
{
	struct ftb_throttler *to_merge=ftb_throttler_deserialize(blob, blob_sz);
	ftb_throttler_merge(t, to_merge);
	ftb_throttler_free(to_merge);
	return;
}
