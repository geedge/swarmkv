#include "ap_bloom.h"
#include "crdt_utils.h"
#include "utlist.h"
#include "utarray.h"

#include <math.h>   // log, ceil
#include <stdio.h>  // printf
#include <assert.h> // assert
#include <stdlib.h> // calloc
#include <string.h> // memset
#include <unistd.h> // write
#include <stdint.h> //uint64_t
#include <stdbool.h>
/* source
 * https://github.com/RedisBloom/RedisBloom/blob/AgePartitionedBF/contrib/agingBloom.c
*/

#define SLICE_EXPANSION 2
#define FILL_RATIO_THRESHOLD 0.5
#define MIN_CAPACITY    1000


//The test_bit_set_bit() code snap is from RedisBloom (BSD License).
//Source https://github.com/RedisBloom/RedisBloom/blob/master/deps/bloom/bloom.c
#define MODE_READ 0
#define MODE_WRITE 1

//return 1 if the bit is already set, 0 if it was not set
inline static int test_bit_set_bit(unsigned char *buf, uint64_t x, int mode) {
    uint64_t byte = x >> 3;
    uint8_t mask = 1 << (x % 8);
    uint8_t c = buf[byte]; // expensive memory access

    if (c & mask) {
        return 1;
    } else {
        if (mode == MODE_WRITE) {
            buf[byte] = c | mask;
        }
        return 0;
    }
}

struct ap_slice
{
    int hash_index;
    int slice_size; //in bytes
    int popcount;   //the number of 1-bits in the slice
    struct timeval last_insert;
    struct timeval first_insert;
    unsigned char * data;
    struct ap_slice * next;
};
static struct ap_slice *ap_slice_new(int slice_size, int hash_index)
{
    struct ap_slice *slice = ALLOC(struct ap_slice, 1);
    slice->slice_size = slice_size;
    assert(slice_size % sizeof(unsigned long long) == 0);
    //ap_slice_chain_deserialize may alloc slice_size=0
    if(slice_size > 0) slice->data = ALLOC(unsigned char, slice_size);
    slice->hash_index = hash_index;
    slice->popcount = 0;
    return slice;
}
static void ap_slice_free(struct ap_slice *slice)
{
    free(slice->data);
    slice->data = NULL;
    free(slice);
}
void ap_slice_chain_info(const struct ap_slice *head, double *fill_ratio, int *slice_num)
{
    const struct ap_slice *slice=NULL;
    *fill_ratio=0;
    *slice_num=0;
    LL_FOREACH(head, slice)
    {
        *fill_ratio = MAX( *fill_ratio, (double)slice->popcount / (slice->slice_size<<3));
        (*slice_num) ++;
        if(slice->next)
        {
            assert(slice->slice_size / slice->next->slice_size == SLICE_EXPANSION);
        }
    }
    if(*slice_num > 1)
    {
        assert(*fill_ratio >= FILL_RATIO_THRESHOLD);
    }
    return;
}
void ap_slice_sanity(const struct ap_slice *head)
{
    const struct ap_slice *slice=NULL;
    LL_FOREACH(head, slice)
    {
        if(slice->next)
        {
            assert(slice->slice_size / slice->next->slice_size == SLICE_EXPANSION);
        }
        assert(slice->first_insert.tv_sec >0 || slice->popcount == 0);
    }
}
static int ap_slice_cmp(const struct ap_slice *a, const struct ap_slice *b)
{
    //bigger size first
    return b->slice_size - a->slice_size;
}
static struct ap_slice * ap_slice_duplicate(const struct ap_slice *slice)
{
    struct ap_slice *new_slice = ap_slice_new(slice->slice_size, slice->hash_index);
    memcpy(new_slice, slice, offsetof(struct ap_slice, data));
    memcpy(new_slice->data, slice->data, slice->slice_size);
    return new_slice;
}
#define HASH_TO_OFFSET(hash, slice) ((hash.a + slice->hash_index * hash.b) % (slice->slice_size<<3))
void ap_slice_add_hash(struct ap_slice *slice, struct double_hash hash, struct timeval now)
{
    int index=double_hash_generate(&hash, slice->hash_index, slice->slice_size<<3);
    int added = !test_bit_set_bit(slice->data, index, MODE_WRITE);

    slice->last_insert = now;
    if(slice->popcount == 0)
    {
        slice->first_insert = now;
    }
    slice->popcount += added;
    return;
}
int ap_slice_check_hash(const struct ap_slice *slice, struct double_hash hash)
{
    int index=double_hash_generate(&hash, slice->hash_index, slice->slice_size<<3);
    return test_bit_set_bit(slice->data, index, MODE_READ);
}
struct ap_slice_event
{
    struct timeval timestamp;
    int slice_size;
    int hash_index;
    int event;//1: start, -1: end
};
int slice_event_cmp(const void *a, const void *b)
{
    const struct ap_slice_event *ea = (struct ap_slice_event*)a;
    const struct ap_slice_event *eb = (struct ap_slice_event*)b;
    if(timercmp(&ea->timestamp, &eb->timestamp, <))
    {
        return -1;
    }
    if(timercmp(&ea->timestamp, &eb->timestamp, >))
    {
        return 1;
    }
    return eb->event - ea->event;
}
UT_icd slice_event_icd = {sizeof(struct ap_slice_event), NULL, NULL, NULL};
struct ap_state
{
    int consecutive_matches;
    int visited_slices;
    int hash_num;
    UT_array slice_time_events;
};
void ap_state_init(struct ap_state *state, int hash_num)
{
    state->consecutive_matches=0;
    state->visited_slices=0;
    state->hash_num=hash_num;
    utarray_init(&state->slice_time_events, &slice_event_icd);
    utarray_reserve(&state->slice_time_events, hash_num*2);
}
void ap_state_clear(struct ap_state *state)
{
    state->consecutive_matches=0;
    state->visited_slices=0;
    utarray_clear(&state->slice_time_events);
}
void ap_state_done(struct ap_state *state)
{
    utarray_done(&state->slice_time_events);
}
int ap_state_is_match(struct ap_state *state)
{
    int counter=0;
    struct ap_slice_event *ev=NULL;
    if(state->consecutive_matches >= state->hash_num)
    {
        if(state->visited_slices==state->consecutive_matches)//fastpath for no slice expansion
        {
            return 1;
        }
        utarray_sort(&state->slice_time_events, slice_event_cmp);
        while((ev=(struct ap_slice_event*)utarray_next(&state->slice_time_events, ev)))
        {
            counter += ev->event;
            if(counter == state->hash_num)
            {
                return 1;
            }
        }
    }
    return 0;
}
static void ap_slice_chain_check_hash(const struct ap_slice *head, struct double_hash hash, struct ap_state *state)
{
    //In a stackable (scalable) Bloom filter, checking for membership now involves inspecting each layer for presence. 
    const struct ap_slice *slice=NULL;
    struct ap_slice_event ev;
    int found=0;
    LL_FOREACH(head, slice)
    {
        if(ap_slice_check_hash(slice, hash))
        {
            ev.timestamp = slice->first_insert;
            ev.event = 1;
            ev.slice_size=slice->slice_size;
            ev.hash_index=slice->hash_index;
            utarray_push_back(&state->slice_time_events, &ev);
            ev.timestamp = slice->last_insert;
            ev.event = -1;
            ev.slice_size = slice->slice_size;
            ev.hash_index = slice->hash_index;
            utarray_push_back(&state->slice_time_events, &ev);
            found=1;
        }
        state->visited_slices++;
    }
    if(found)
    {
        state->consecutive_matches++;
    }
    else
    {
        ap_state_clear(state);
    }
    return;
}

 /* Return:
 * -------
 *     0 - element was not present and was added
 *     1 - element (or a collision) had already been added previously
 */
static void ap_slice_chain_add_hash(struct ap_slice **head, struct double_hash hash, struct timeval now)
{
    //add new slice if the current slice is full
    if( (double) (*head)->popcount / ((*head)->slice_size<<3) > FILL_RATIO_THRESHOLD)
    {
        struct ap_slice *new_slice = ap_slice_new((*head)->slice_size * SLICE_EXPANSION, (*head)->hash_index);
        LL_PREPEND((*head), new_slice);
    }
    //Add it to the current (head) slice.
    ap_slice_add_hash(*head, hash, now);
    return;
}
void ap_slice_chain_first_insert_time(const struct ap_slice *head, struct timeval *first_insert)
{
    const struct ap_slice *slice=NULL;
    *first_insert = head->first_insert;
    LL_FOREACH(head, slice)
    {
        if(timercmp(&slice->first_insert, first_insert, <))
        {
            *first_insert = slice->first_insert;
        }
    }
    return;
}
static void ap_slice_chain_free(struct ap_slice *head)
{
    struct ap_slice *slice=NULL, *tmp=NULL;
    LL_FOREACH_SAFE(head, slice, tmp)
    {
        LL_DELETE(head, slice);
        ap_slice_free(slice);
    }
}
static void ap_slice_chain_reset(struct ap_slice *head)
{
    struct ap_slice *slice=NULL, *tmp=NULL;
    LL_FOREACH_SAFE(head, slice, tmp)
    {
        if(slice == head) continue;
        LL_DELETE(head, slice);
        ap_slice_free(slice);
    }
    head->popcount = 0;
    head->first_insert.tv_sec = head->last_insert.tv_sec = 0;
    head->first_insert.tv_usec = head->last_insert.tv_usec = 0;
    memset(head->data, 0, head->slice_size);
    return;
}
static struct ap_slice *ap_slice_chain_duplicate(const struct ap_slice *src)
{
    struct ap_slice *new_head=NULL;
    const struct ap_slice *slice=NULL;
    LL_FOREACH(src, slice)
    {
        struct ap_slice * new_slice=ap_slice_duplicate(slice);
        LL_APPEND(new_head, new_slice);
    }
    return new_head;
}
static size_t ap_slice_chain_mem_size(const struct ap_slice *head)
{
    size_t sz=0;
    const struct ap_slice *slice=NULL;
    LL_FOREACH(head, slice)
    {
        sz += sizeof(struct ap_slice);
        sz += slice->slice_size;
    }
    return sz;
}
struct slice_chain_header
{
    int chain_sequence;
    int slice_number;
    size_t chain_size;
};
static size_t ap_slice_chain_serialize_size(const struct ap_slice *head)
{
    size_t sz=0;
    const struct ap_slice *slice=NULL;
    sz += sizeof(struct slice_chain_header);
    LL_FOREACH(head, slice)
    {
        sz += offsetof(struct ap_slice, data);
        sz += slice->slice_size;
    }
    return sz;
}
static size_t ap_slice_chain_serialize(const struct ap_slice *head, int chain_sequence, char *buffer, size_t buffer_sz)
{
    size_t offset=0;
    
    struct slice_chain_header *header=(struct slice_chain_header*)(buffer+offset);
    header->chain_sequence=chain_sequence;
    header->chain_size=0;
    header->slice_number=0;
    offset += sizeof(struct slice_chain_header);
    const struct ap_slice *slice=NULL;
    LL_FOREACH(head, slice)
    {
        memcpy(buffer+offset, slice, offsetof(struct ap_slice, data));
        header->chain_size += offsetof(struct ap_slice, data);
        offset += offsetof(struct ap_slice, data);
        memcpy(buffer+offset, slice->data, slice->slice_size);
        offset += slice->slice_size;
        header->chain_size += slice->slice_size;
        header->slice_number++;
        if(slice->next)
        {
            assert(slice->slice_size / slice->next->slice_size == SLICE_EXPANSION);
        }
    }
    assert(offset<=buffer_sz);
    return offset;
}
static struct ap_slice *ap_slice_chain_deserialize(const char *buffer, size_t buffer_sz)
{
    struct slice_chain_header *header=(struct slice_chain_header*)buffer;
    size_t offset=sizeof(struct slice_chain_header);
    struct ap_slice *head=NULL;
    for(size_t i=0; i<header->slice_number; i++)
    {
        struct ap_slice *slice = ap_slice_new(0, 0);
        memcpy(slice, buffer+offset, offsetof(struct ap_slice, data));
        offset += offsetof(struct ap_slice, data);
        slice->data = (unsigned char*) malloc(slice->slice_size);
        memcpy(slice->data, buffer+offset, slice->slice_size);
        offset += slice->slice_size;
        LL_APPEND(head, slice);
        if(head->next)
        {
            assert(head->slice_size / head->next->slice_size == SLICE_EXPANSION);
        }
    }
    return head;
}
static void ap_slice_merge(struct ap_slice *dst, const struct ap_slice *src)
{
    assert(dst->hash_index == src->hash_index);
    assert(dst->slice_size == src->slice_size);
    if(src->popcount == 0)
    {
        return;
    }
    if(dst->popcount == 0)
    {
        memcpy(dst, src, offsetof(struct ap_slice, data));
        memcpy(dst->data, src->data, src->slice_size);
        return;
    }
    int popcnt=0;
    for(int i=0; i<dst->slice_size/sizeof(unsigned long long); i++)
    {
        ((unsigned long long*)dst->data)[i] |= ((unsigned long long*)src->data)[i];
        popcnt += __builtin_popcountll( ((unsigned long long*)dst->data)[i]);
    }
    dst->popcount = popcnt;
    if(timercmp(&(dst->last_insert), &(src->last_insert), <))
    {
        dst->last_insert = src->last_insert;
    }
    if(timercmp(&(dst->first_insert), &(src->first_insert), >))
    {
        dst->first_insert = src->first_insert;
    }
    return;
}
static void ap_slice_chain_merge(struct ap_slice **dst_head, const struct ap_slice *src_head)
{
    const struct ap_slice *src_slice=NULL;
    int merged=0;
 
    ap_slice_sanity(*dst_head);
    ap_slice_sanity(src_head);

    LL_FOREACH(src_head, src_slice)
    {
        struct ap_slice *dst_slice=NULL;
        merged=0;
        LL_FOREACH(*dst_head, dst_slice)
        {
            if(src_slice->slice_size == dst_slice->slice_size)
            {
                ap_slice_merge(dst_slice, src_slice);
                merged=1;
                break;
            }
        }
        if(!merged)
        {
            struct ap_slice *new_slice = ap_slice_duplicate(src_slice);
            LL_APPEND(*dst_head, new_slice);
        }
    }
    LL_SORT(*dst_head, ap_slice_cmp);
    ap_slice_sanity(*dst_head);
}
struct AP_configuration
{
    double error;
    long long capacity;
    long long time_window_ms;
    long long time_slice_num;
    struct timeval last_cfg;
};
struct AP_bloom
{
    //high level variables determined by caller
    struct AP_configuration cfg;

    //low level variables caculated by the call configure
    int hash_num;    //k
    int chain_num;  //hash_num+time_slice_num
    int default_slice_size; // in bytes
    
    //runtime variables
    int cursor;
    struct timeval last_slide;
    struct ap_slice **heads; //hash_num+timeframe_num
};
#define DBL_EPSILON 2.2204460492503131e-16
bool definitelyLessThan(float a, float b)
{
    return (b - a) > ( (fabs(a) < fabs(b) ? fabs(b) : fabs(a)) * DBL_EPSILON);
}
struct AP_bloom *AP_bloom_new(struct timeval now, double error_rate, long long capacity, long long time_window_ms, long long time_slice_num)
{
    if (time_slice_num < 0)
    {
        return NULL;
    }
    struct AP_bloom *bloom = ALLOC(struct AP_bloom, 1);
    bloom->cfg.time_slice_num = time_slice_num;
    bloom->hash_num = ceil(log2(1/error_rate));
    if(capacity < MIN_CAPACITY)
    {
        capacity = MIN_CAPACITY;
    }
    bloom->cfg.error = error_rate;
    bloom->cfg.capacity = capacity;
    bloom->cfg.time_window_ms = time_window_ms;
    bloom->cfg.last_cfg = now;
    bloom->cfg.time_slice_num = time_slice_num;

    bloom->last_slide = now;
    bloom->cursor = 0;
    bloom->default_slice_size = ceil(capacity/log(2));
    bloom->default_slice_size = ceil((double)bloom->default_slice_size/64)*64/8;

    bloom->chain_num = bloom->hash_num + bloom->cfg.time_slice_num;
    bloom->heads = ALLOC(struct ap_slice*, bloom->chain_num);
    for(int i=0; i<bloom->chain_num; i++)
    {
        bloom->heads[i] = ap_slice_new(bloom->default_slice_size, i % bloom->chain_num);
    }
    return bloom;
}

static void slide_time(struct AP_bloom *bloom, struct timeval now)
{
    int chain_num = bloom->chain_num;
    long long slide_time_frame_us=bloom->cfg.time_slice_num?(bloom->cfg.time_window_ms*1000/bloom->cfg.time_slice_num):INT64_MAX;
    long long slide_us=timeval_delta_us(bloom->last_slide, now);
    long long elapse_us=timeval_delta_us(bloom->cfg.last_cfg, now);
    long long epoches=elapse_us/slide_time_frame_us;
    
    if(slide_us < slide_time_frame_us)
    {
        return;
    }
    slide_us -= slide_us % slide_time_frame_us;
    int n_slide = slide_us/slide_time_frame_us;
    
    if(n_slide < bloom->cfg.time_slice_num)
    {
        for(int i=0; i<n_slide; i++)
        {
            int reset_idx = (bloom->cursor + bloom->hash_num) % chain_num;
            ap_slice_chain_reset(bloom->heads[reset_idx]);
            bloom->cursor = (bloom->cursor + 1) % chain_num;
        }
    }
    else
    {
        for(int i=0; i<chain_num; i++)
        {
            ap_slice_chain_reset(bloom->heads[i]);
        }
        bloom->cursor = (bloom->cursor + n_slide) % chain_num;
    }
    struct timeval slide_time;
    slide_time.tv_sec = slide_us/1000/1000;
    slide_time.tv_usec = slide_us%(1000*1000);
    timeradd(&bloom->last_slide, &slide_time, &bloom->last_slide);

    assert(bloom->cursor == (epoches % chain_num) || bloom->cursor == (epoches % chain_num + 1) % chain_num);
    return;
}

void AP_bloom_add(struct AP_bloom *bloom, struct timeval now, const char *buffer, int len)
{
    struct double_hash hash;
    double_hash_init(&hash, buffer, len);
    if(bloom->cfg.time_window_ms)
    {
        slide_time(bloom, now);
    }
    for(int i=0; i<bloom->hash_num; i++)
    {
        int idx=(bloom->cursor+i) % (bloom->chain_num);
        ap_slice_chain_add_hash(&(bloom->heads[idx]), hash, now);
    }
    return;
}
static int is_slice_expired(const struct ap_slice *slice, struct timeval now, long long time_window_ms)
{
    if(time_window_ms == 0)
    {
        return 0;
    }
    long long delta_us = timeval_delta_us(slice->last_insert, now);
    return delta_us > time_window_ms*1000;
}
int AP_bloom_check(const struct AP_bloom *bloom, struct timeval now, const char *buffer, int len)
{
    if(timeval_delta_ms(bloom->cfg.last_cfg, now) < 0)
    {
        return 0;
    }
    struct double_hash hash;
    double_hash_init(&hash, buffer, len);
    int chain_num = bloom->chain_num;
    //FAST PATH for no slice expansion
    int consecutive_matches=0, i =0;
    for(i = 0; i < bloom->hash_num+chain_num; i++)
    {
        if(bloom->heads[i%chain_num]->next) break;
        if(is_slice_expired(bloom->heads[i%chain_num], now, bloom->cfg.time_window_ms))
        {
            consecutive_matches=0;
        }
        if(ap_slice_check_hash(bloom->heads[i%chain_num], hash))
        {
            consecutive_matches++;
        }
        else
        {
            consecutive_matches = 0;
        }
        if (consecutive_matches == bloom->hash_num)
        {
           return 1;
        }
    }
    if(i == bloom->hash_num+chain_num)
    {
        return 0;
    }
    //SLOW PATH for slice expansion
    struct ap_state state;
    ap_state_init(&state, bloom->hash_num);
    for(i = 0; i < bloom->hash_num+chain_num; i++)
    {
        if(is_slice_expired(bloom->heads[i%chain_num], now, bloom->cfg.time_window_ms))
        {
            ap_state_clear(&state);
            continue;
        }
        ap_slice_chain_check_hash(bloom->heads[i%chain_num], hash, &state);
        if(ap_state_is_match(&state))
        {
            ap_state_done(&state);
            return 1;
        }
    }
    ap_state_done(&state);
    return 0;
}
void AP_bloom_free(struct AP_bloom *bloom)
{
    for(int i=0; i<bloom->chain_num; i++)
    {
        ap_slice_chain_free(bloom->heads[i]);
        bloom->heads[i]=NULL;
    }
    free(bloom->heads);
    bloom->heads=NULL;
    free(bloom);
}
size_t AP_bloom_mem_size(const struct AP_bloom *bloom)
{
    size_t sz=0;
    sz += sizeof(struct AP_bloom);
    sz += sizeof(struct ap_slice*)*bloom->chain_num;
    for(int i=0; i<bloom->chain_num; i++)
    {
        sz += ap_slice_chain_mem_size(bloom->heads[i]);
    }
    return sz;
}
size_t AP_bloom_serialize_size(const struct AP_bloom *bloom)
{
    size_t sz=0;
    sz += offsetof(struct AP_bloom, heads);
    for(int i=0; i<bloom->chain_num; i++)
    {
        sz+=ap_slice_chain_serialize_size(bloom->heads[i]);
    }
    return sz;
}
void AP_bloom_serialize(const struct AP_bloom *bloom, char **blob, size_t *blob_sz)
{
    size_t sz=AP_bloom_serialize_size(bloom);
    size_t offset=0;
    char *buffer = (char*) malloc(sz);
    memcpy(buffer, bloom, offsetof(struct AP_bloom, heads));
    offset += offsetof(struct AP_bloom, heads);
    for(int i=0; i<bloom->chain_num; i++)
    {
        offset += ap_slice_chain_serialize(bloom->heads[i], i, buffer+offset, sz-offset);
    }
    assert(offset==sz);
    *blob_sz=sz;
    *blob=buffer;
}
struct AP_bloom * AP_bloom_deserialize(const char *blob, size_t blob_sz)
{
    struct AP_bloom *bloom=ALLOC(struct AP_bloom, 1);
    size_t offset=0;
    memcpy(bloom, blob+offset, offsetof(struct AP_bloom, heads));
    offset += offsetof(struct AP_bloom, heads);
    bloom->heads=ALLOC(struct ap_slice*, bloom->chain_num);
    for(int i=0; i<bloom->chain_num; i++)
    {
        bloom->heads[i]=ap_slice_chain_deserialize(blob+offset, blob_sz-offset);
        offset += ap_slice_chain_serialize_size(bloom->heads[i]);
    }
    assert(offset==blob_sz);
    return bloom;
}
struct AP_bloom *AP_bloom_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
    struct AP_bloom *bloom=AP_bloom_deserialize(blob, blob_sz);
    return bloom;
}
void AP_bloom_merge(struct AP_bloom *dst, const struct AP_bloom *src)
{
    if(memcmp(&dst->cfg, &src->cfg, sizeof(dst->cfg)))
    {
        if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), <))
        {
            for(int i=0; i<dst->chain_num; i++)
            {
                ap_slice_chain_free(dst->heads[i]);
            }
            
            memcpy(dst, src, offsetof(struct AP_bloom, heads));
            free(dst->heads);
            dst->heads = ALLOC(struct ap_slice*, dst->chain_num);
            for(int i=0; i<dst->chain_num; i++)
            {
                dst->heads[i] = ap_slice_chain_duplicate(src->heads[i]);
            }
        }
        return;
    }
    if(timercmp(&(dst->last_slide), &(src->last_slide), <))
    {
        slide_time(dst, src->last_slide);
    }
    assert(src->chain_num == dst->chain_num);
    long long slide_time_frame_us = dst->cfg.time_window_ms?(dst->cfg.time_window_ms*1000/dst->cfg.time_slice_num):0;
    for(int i=0; i<src->chain_num; i++)
    {
        struct timeval src_first_insert, dst_first_insert;
        ap_slice_chain_first_insert_time(src->heads[i], &src_first_insert);
        ap_slice_chain_first_insert_time(dst->heads[i], &dst_first_insert);
        long long delta_us = timeval_delta_us(src_first_insert, dst_first_insert);
        if(delta_us>slide_time_frame_us)
        {
            continue;
        }
        ap_slice_chain_merge(&(dst->heads[i]), src->heads[i]);
    }
    return;
}
void AP_bloom_merge_blob(struct AP_bloom *dst, const char *blob, size_t blob_sz)
{
    struct AP_bloom *src=AP_bloom_deserialize(blob, blob_sz);
    AP_bloom_merge(dst, src);
    AP_bloom_free(src);
}
long long approximate_item_num(const struct AP_bloom *bloom)
{
    //https://en.wikipedia.org/wiki/Bloom_filter#Approximating_the_number_of_items_in_a_Bloom_filter
    //element_num = - (m/k) * ln(1 - popcnt/m)
    //m is the length (size) of the filter, k is the number of hash functions, and X is the number of bits set to one.
    long long element_num=0;
    long long m=0;
    long long X=0;
    for(int i=0; i<bloom->chain_num; i++)
    {
        const struct ap_slice *slice=NULL;
        LL_FOREACH(bloom->heads[i], slice)
        {
            m += slice->slice_size;
            X += slice->popcount;
        }
    }
    m=m<<3;//byte size to bit size
    element_num=ceil(-(m/bloom->chain_num)*log(1-(double)X/m));
    return element_num;
}
long long AP_bloom_cardinality(const struct AP_bloom *bloom)
{
    return approximate_item_num(bloom);
}
void AP_bloom_info(const struct AP_bloom *bloom, struct AP_bloom_info *info)
{
    //The actual error rate is more complexity.
    //https://mathoverflow.net/questions/288357/number-of-binary-arrays-of-length-n-with-k-consecutive-1s
    info->error = bloom->cfg.error;
    info->capacity = bloom->cfg.capacity;
    info->time_window_ms = bloom->cfg.time_window_ms;
    info->hash_num = bloom->hash_num;
    info->time_slice_num = bloom->cfg.time_slice_num;
    info->total_slice_number = 0;
    info->max_expand_times = 0;
    info->fill_ratio = 0;
    struct timeval first_insert[bloom->chain_num];
    struct timeval oldest_item=bloom->last_slide;
    for(int i=0; i<bloom->chain_num; i++)
    {
        struct ap_slice *slice=NULL;
        int slice_num=0;
        double fill_ratio = 0;
        ap_slice_chain_info(bloom->heads[i], &fill_ratio, &slice_num);
        LL_COUNT(bloom->heads[i], slice, slice_num);
        info->total_slice_number += slice_num;
        info->max_expand_times = MAX(info->max_expand_times, slice_num);
        info->fill_ratio = MAX(info->fill_ratio, fill_ratio);
        ap_slice_chain_first_insert_time(bloom->heads[i], &first_insert[i]);
    }
    
    for(int i=0; i<bloom->chain_num+bloom->hash_num; i++)
    {
        struct timeval per_window_oldest_item;
        per_window_oldest_item.tv_sec=0;
        per_window_oldest_item.tv_usec=0;
        
        for(int j=0; j<bloom->hash_num; j++)
        {
            int idx = (i+j)%bloom->chain_num;
            //time of per window oldest item is MAX(first_insert[i], ... , first_insert[i+hash_num-1]).
            if(timercmp(&first_insert[idx], &per_window_oldest_item, >))
            {
                per_window_oldest_item = first_insert[idx];
            }
        }
        //time of global oldest item is MIN(per_window_oldest_item).
        if(timercmp(&per_window_oldest_item, &oldest_item, <))
        {
            oldest_item = per_window_oldest_item;
        }
    }
    info->approximate_item_num = approximate_item_num(bloom);
    info->oldest_item_time = oldest_item;
    return;
}