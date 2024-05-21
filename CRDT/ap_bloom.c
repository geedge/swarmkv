#include "ap_bloom.h"
#include "crdt_utils.h"
#include "utlist.h"
#include "utarray.h"
#include "xxhash.h"
#include <math.h>   // log, ceil
#include <stdio.h>  // printf
#include <assert.h> // assert
#include <stdlib.h> // calloc
#include <string.h> // memset
#include <unistd.h> // write
#include <stdint.h> //uint64_t
#include <stdbool.h>

#define APBM_USE_MEMPOOL 1   //use mempool to reduce page-faults
#if APBM_USE_MEMPOOL
#define APMOOL_ALLOC(mp, type, number) ((type *)apbm_mempool_alloc(mp, sizeof(type)*(number)))
#define APMOOL_FREE(mp, p)  {apbm_mempool_free(mp, p); p=NULL;}
#else
#define APMOOL_ALLOC(mp, type, number) ALLOC(type, number)
#define APMOOL_FREE(mp, p) FREE(&p)
#endif

#define USE_HUGE_PAGE           (0) //precondition : sysctl -w vm.nr_hugepages=128,256,512... or other number by your needs
#define MEM_POOL_INIT_BLOCK_NUM (24) 
#define MEM_ALIGN_SIZE()        (getpagesize() > 4096 ? 4096: getpagesize())

#if APBM_USE_MEMPOOL
struct apbm_block_info{
    void *block_memory_head; 
    void *block_memory_tail;
    void **block_heads;
    int *block_inused_flags;
    unsigned int block_size;
    unsigned int block_num;
    int ues_hugepage;
    int to_flush_block_num;
};
struct apbm_mempool{
    struct apbm_block_info large;
    struct apbm_block_info small;
};
enum mempool_flag{
    MEMPOOL_USABLE = 0,
    MEMPOOL_INUSED = 1,
};  

static int apbm_mempool_block_init(struct apbm_block_info *block, size_t block_size, int block_num)
{
    block->block_size = block_size;
    block->block_num = block_num;
#if USE_HUGE_PAGE
    block->block_memory_head = mmap(NULL, block_num * block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if(MAP_FAILED == block->block_memory_head){
        perror("dabm_mempool_init() mmap huge-page error, use glibc: ");
        posix_memalign((void **)&block->block_memory_head, 64, block->block_num * block->block_size);
        block->ues_hugepage = 0;
    }else{
        block->ues_hugepage = 1;
    }
#else
    posix_memalign((void **)&block->block_memory_head, MEM_ALIGN_SIZE(), block->block_num * block->block_size);
#endif
    block->block_memory_tail = (char *)block->block_memory_head + block->block_num * block->block_size;
    memset(block->block_memory_head, 0, block->block_num * block->block_size);
    block->block_heads = (void **)calloc(block->block_num, sizeof(void *));
    block->block_inused_flags = (int *)calloc(block->block_num, sizeof(int));
    for(int i=0; i<block->block_num; i++)
    {
        block->block_heads[i] = (char *)block->block_memory_head + i * block->block_size;
        block->block_inused_flags[i] = 0;
    }
    return 0;
}
static void apbm_mp_block_free(struct apbm_block_info *block, void *p)
{
    for (int i = 0; i < block->block_num; i++)
    {
        if((block->block_heads[i] == p) 
        && (block->block_inused_flags[i] == MEMPOOL_INUSED)){          
            memset(p, 0, block->block_size); //memset in free()
            block->block_inused_flags[i] = MEMPOOL_USABLE;     
            return;
        }
    }
    assert(0);
}
static void apbm_mp_block_destroy(struct apbm_block_info *block)
{
    if(block->block_memory_head){
        if(block->ues_hugepage){
            munmap(block->block_memory_head, block->block_num * block->block_size);
        }else{
            free(block->block_memory_head);
        }
        block->block_memory_head = NULL;
    }
    if(block->block_heads){
        free(block->block_heads);
        block->block_heads = NULL;
    }
    if(block->block_inused_flags){
        free(block->block_inused_flags);
        block->block_inused_flags = NULL;
    }
    block->block_num = 0;
    block->block_size = 0;
}
static void *apbm_mp_block_alloc(struct apbm_block_info *block, int size)
{
    for (int i = 0; i < block->block_num; i++){
        if(block->block_inused_flags[i] == MEMPOOL_USABLE){
            block->block_inused_flags[i] = MEMPOOL_INUSED;
            return block->block_heads[i];
        }
    }
    return NULL;
}

void *apbm_mempool_alloc(struct apbm_mempool *mp, int size)
{
    void *res = NULL;
    if(size <= mp->small.block_size){
        res = apbm_mp_block_alloc(&mp->small, size);
    }else if(size <= mp->large.block_size){
        res = apbm_mp_block_alloc(&mp->large, size);
    }
    if(NULL == res){
        //mempool full or size unmatch, alloc use glibc
        res = calloc(size, sizeof(char));
    }
    return res;
}
void apbm_mempool_free(struct apbm_mempool *mp, void *p)
{
    if((unsigned long long)p >= (unsigned long long)mp->small.block_memory_head 
    && (unsigned long long)p <= (unsigned long long)mp->small.block_memory_tail){
        return apbm_mp_block_free(&mp->small, p);
    }
    if((unsigned long long)p >= (unsigned long long)mp->large.block_memory_head 
    && (unsigned long long)p <= (unsigned long long)mp->large.block_memory_tail){
        return apbm_mp_block_free(&mp->large, p);
    }
    //not allocated from mempool
    free(p);
}
struct apbm_mempool *apbm_mempool_new(size_t large_block_size, int large_block_num, size_t small_block_size, int small_block_num)
{
    struct apbm_mempool *mp = (struct apbm_mempool *)calloc(1, sizeof(struct apbm_mempool));
    memset(mp, 0, sizeof(struct apbm_mempool));
    apbm_mempool_block_init(&mp->large, large_block_size, large_block_num);
    apbm_mempool_block_init(&mp->small, small_block_size, small_block_num);
    return mp;
}
void apbm_mempool_destroy(struct apbm_mempool *mp)
{
    apbm_mp_block_destroy(&mp->large);
    apbm_mp_block_destroy(&mp->small);
    free(mp);
}
#endif


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

struct AP_bloom_stat{
    size_t slice_new_num_acc;
    size_t slice_free_num_acc;
    size_t expand_num_acc;
    size_t expand_max_multiple_rt;
    size_t slice_num_rt;
};
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

    struct AP_bloom_stat stat;
    struct apbm_mempool *mp;
};

//return 1 if the bit is already set, 0 if it was not set
inline static int set_bit(unsigned char *buf, uint32_t bit_index, uint32_t byte_index) {
    uint8_t mask = 1 << (bit_index % 8);
    uint8_t c = buf[byte_index]; // expensive memory access, maybe trigger page-fault events
    if (c & mask) {
        return 1;
    } else {
        buf[byte_index] = c | mask;
        return 0;
    }
}

inline static int test_bit(const unsigned char *buf, uint32_t bit_index, uint32_t byte_index) {
    uint8_t mask = 1 << (bit_index % 8);
    uint8_t c = buf[byte_index]; // expensive memory access, maybe trigger page-fault events
    return c & mask;
}

struct ap_slice
{
    unsigned int hash_index;
    int popcount;   //the number of 1-bits in the slice
    unsigned int slice_size; //in bytes
    struct timeval last_insert;
    struct timeval first_insert;
    unsigned char * data;
    struct ap_slice * next;
};
static struct ap_slice *ap_slice_new(struct AP_bloom *bloom, int slice_size, int hash_index)
{
    struct ap_slice *slice = APMOOL_ALLOC(bloom->mp, struct ap_slice, 1);
    slice->slice_size = slice_size;
    assert(slice_size % sizeof(unsigned long long) == 0);
    //ap_slice_chain_deserialize may alloc slice_size=0
    if(slice_size > 0){ 
        slice->data = APMOOL_ALLOC(bloom->mp, unsigned char, slice_size);
        memset(slice->data, 0, slice_size);
    }
    slice->hash_index = hash_index;
    slice->popcount = 0;

    bloom->stat.slice_new_num_acc++;
    bloom->stat.slice_num_rt++;
    return slice;
}
static void ap_slice_free(struct AP_bloom *bloom, struct ap_slice *slice)
{
    APMOOL_FREE(bloom->mp, slice->data);
    // slice->data = NULL;
    APMOOL_FREE(bloom->mp, slice);
    bloom->stat.slice_free_num_acc++;
    bloom->stat.slice_num_rt--;
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
static struct ap_slice * ap_slice_duplicate(struct AP_bloom *bloom, const struct ap_slice *slice)
{
    struct ap_slice *new_slice = ap_slice_new(bloom, slice->slice_size, slice->hash_index);
    memcpy(new_slice, slice, offsetof(struct ap_slice, data));
    memcpy(new_slice->data, slice->data, slice->slice_size);
    return new_slice;
}
#define HASH_TO_OFFSET(hash, slice) ((hash.a + slice->hash_index * hash.b) % (slice->slice_size<<3))
void ap_slice_add_hash(struct ap_slice *slice, const struct double_hash *hash, struct timeval now)
{
    unsigned int bit_index=double_hash_generate(hash, slice->hash_index, slice->slice_size<<3);
    unsigned int byte_index = bit_index>>3;
    __builtin_prefetch(&slice->data[byte_index], 1);
    int added = !set_bit(slice->data, bit_index, byte_index);

    slice->popcount += added;
    slice->last_insert = now;
    if(unlikely(slice->popcount == 0))
    {
        slice->first_insert = now;
    }
    return;
}
int ap_slice_check_hash(const struct ap_slice *slice, const struct double_hash *hash)
{
    unsigned int bit_index=double_hash_generate(hash, slice->hash_index, slice->slice_size<<3);
    unsigned int byte_index = bit_index>>3;
    __builtin_prefetch(&slice->data[byte_index], 0);
    return test_bit(slice->data, bit_index, byte_index);
}

static int ap_slice_chain_check_hash(const struct ap_slice *head, const struct double_hash *hash)
{
    TIME_RECORD();
    //In a stackable (scalable) Bloom filter, checking for membership now involves inspecting each layer for presence. 
    const struct ap_slice *slice=NULL;
    int found=0;
    LL_FOREACH(head, slice)
    {
        if(ap_slice_check_hash(slice, hash))
        {
            found=1;
            break; //quick path
        }
    }
    TIME_DIFF();
    return found;
}

 /* Return:
 * -------
 *     0 - element was not present and was added
 *     1 - element (or a collision) had already been added previously
 */
static void ap_slice_chain_add_hash(struct AP_bloom *bloom, struct ap_slice **head, const struct double_hash *hash, struct timeval now)
{
    //add new slice if the current slice is full
    if(unlikely( (double) (*head)->popcount / ((*head)->slice_size<<3) > FILL_RATIO_THRESHOLD))
    {
        struct ap_slice *new_slice = ap_slice_new(bloom, (*head)->slice_size * SLICE_EXPANSION, (*head)->hash_index);
        LL_PREPEND((*head), new_slice);
        bloom->stat.expand_num_acc++;
        if(new_slice->slice_size/bloom->default_slice_size > bloom->stat.expand_max_multiple_rt){
            bloom->stat.expand_max_multiple_rt = new_slice->slice_size/bloom->default_slice_size;
        }
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
static void ap_slice_chain_free(struct AP_bloom *bloom, struct ap_slice *head)
{
    struct ap_slice *slice=NULL, *tmp=NULL;
    LL_FOREACH_SAFE(head, slice, tmp)
    {
        LL_DELETE(head, slice);
        ap_slice_free(bloom, slice);
    }
}
static struct ap_slice *ap_slice_chain_duplicate(struct AP_bloom *bloom, const struct ap_slice *src)
{
    struct ap_slice *new_head=NULL;
    const struct ap_slice *slice=NULL;
    LL_FOREACH(src, slice)
    {
        struct ap_slice * new_slice=ap_slice_duplicate(bloom, slice);
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
static size_t ap_slice_chain_mem_blocks(const struct ap_slice *head)
{
    size_t blocks=0;
    const struct ap_slice *slice=NULL;
    LL_FOREACH(head, slice)
    {
        blocks += 2;
    }
    return blocks;
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
static struct ap_slice *ap_slice_chain_deserialize(struct AP_bloom *bloom, const char *buffer, size_t buffer_sz)
{
    struct slice_chain_header *header=(struct slice_chain_header*)buffer;
    size_t offset=sizeof(struct slice_chain_header);
    struct ap_slice *head=NULL;
    for(size_t i=0; i<header->slice_number; i++)
    {
        struct ap_slice *slice = ap_slice_new(bloom, 0, 0);
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
static void ap_slice_merge(struct AP_bloom *bloom, struct ap_slice *dst, const struct ap_slice *src)
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
static void ap_slice_chain_merge(struct AP_bloom *bloom, struct ap_slice **dst_head, const struct ap_slice *src_head)
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
                ap_slice_merge(bloom, dst_slice, src_slice);
                merged=1;
                break;
            }
        }
        if(!merged)
        {
            struct ap_slice *new_slice = ap_slice_duplicate(bloom, src_slice);
            LL_APPEND(*dst_head, new_slice);
        }
    }
    LL_SORT(*dst_head, ap_slice_cmp);
    ap_slice_sanity(*dst_head);
}
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

    bloom->cursor = 0;
    bloom->default_slice_size = ceil(capacity/log(2));
    bloom->default_slice_size = ceil((double)bloom->default_slice_size/64)*64/8;
    if(bloom->default_slice_size % MEM_ALIGN_SIZE() != 0){
        bloom->default_slice_size = (bloom->default_slice_size / MEM_ALIGN_SIZE() + 1) * MEM_ALIGN_SIZE();
    }
    assert(0 == bloom->default_slice_size % MEM_ALIGN_SIZE());
#if APBM_USE_MEMPOOL
    bloom->mp = apbm_mempool_new(bloom->default_slice_size, MEM_POOL_INIT_BLOCK_NUM, 128, MEM_POOL_INIT_BLOCK_NUM);
#endif

    bloom->chain_num = bloom->hash_num + bloom->cfg.time_slice_num;
    bloom->heads = APMOOL_ALLOC(bloom->mp, struct ap_slice*, bloom->chain_num);
    for(int i=0; i<bloom->chain_num; i++)
    {
        bloom->heads[i] = ap_slice_new(bloom, bloom->default_slice_size, i % bloom->chain_num);
    }
    bloom->stat.expand_max_multiple_rt = 1;
    bloom->stat.slice_num_rt = bloom->chain_num;
    bloom->last_slide = now;
    return bloom;
}

static void slide_time(struct AP_bloom *bloom, struct timeval now)
{
    int chain_num = bloom->chain_num;
    long long slide_time_frame_us=bloom->cfg.time_slice_num?(bloom->cfg.time_window_ms*1000/bloom->cfg.time_slice_num):INT64_MAX;
    long long slide_us=timeval_delta_us(bloom->last_slide, now);
    
    if(likely(slide_us < slide_time_frame_us))
    {
        return;
    }
    TIME_RECORD();
    long long elapse_us=timeval_delta_us(bloom->cfg.last_cfg, now);
    long long epoches=elapse_us/slide_time_frame_us;    
    slide_us -= slide_us % slide_time_frame_us;
    int n_slide = slide_us/slide_time_frame_us;
    int reset_hash_index;
    if(n_slide < bloom->cfg.time_slice_num)
    {
        for(int i=0; i<n_slide; i++)
        {
            int reset_idx = (bloom->cursor + bloom->hash_num) % chain_num;
            reset_hash_index = bloom->heads[reset_idx]->hash_index;
            ap_slice_chain_free(bloom, bloom->heads[reset_idx]);
            struct ap_slice *slice = ap_slice_new(bloom, bloom->default_slice_size, reset_hash_index);
            bloom->heads[reset_idx] = slice;
            bloom->cursor = (bloom->cursor + 1) % chain_num;
        }
    }
    else
    {
        for(int i=0; i<chain_num; i++)
        {
            reset_hash_index = bloom->heads[i]->hash_index;
            ap_slice_chain_free(bloom, bloom->heads[i]);
            struct ap_slice *new_slice=ap_slice_new(bloom, bloom->default_slice_size, reset_hash_index);
            bloom->heads[i] = new_slice;
        }
        bloom->cursor = (bloom->cursor + n_slide) % chain_num;
    }

    struct timeval slide_time;
    slide_time.tv_sec = slide_us/1000/1000;
    slide_time.tv_usec = slide_us%(1000*1000);
    timeradd(&bloom->last_slide, &slide_time, &bloom->last_slide);

    assert(bloom->cursor == (epoches % chain_num) || bloom->cursor == (epoches % chain_num + 1) % chain_num);
    TIME_DIFF();
    return;
}

void AP_bloom_add(struct AP_bloom *bloom, struct timeval now, const char *buffer, int len)
{
    TIME_RECORD();
    struct double_hash hash; 
    double_hash_init(&hash, buffer, (size_t)len);
    if(bloom->cfg.time_slice_num && bloom->cfg.time_window_ms)
    {
        slide_time(bloom, now);
    }
    for(int i=0; i<bloom->hash_num; i++)
    {
        int idx=(bloom->cursor+i) % (bloom->chain_num);
        ap_slice_chain_add_hash(bloom,&(bloom->heads[idx]), &hash, now);
    }
    TIME_DIFF();
    return;
}

int AP_bloom_check(const struct AP_bloom *bloom, struct timeval now, const char *buffer, int len)
{
    if(unlikely(timeval_delta_ms(bloom->cfg.last_cfg, now) < 0))
    {
        return 0;
    }
    TIME_RECORD();  
    struct double_hash hash;
    double_hash_init(&hash, buffer, (size_t)len);
    int chain_num = bloom->chain_num;
    int match_num = 0, unmatch_num = 0;
    for(int i = 0; i < chain_num; i++)
    {
        long long delta_us = timeval_delta_us(bloom->heads[i]->last_insert, now);
        if(unlikely((delta_us > bloom->cfg.time_window_ms*1000) 
           && bloom->cfg.time_window_ms))
        {
            continue;
        }
        if(ap_slice_chain_check_hash(bloom->heads[i], &hash)){
            match_num++;
        }else{
            unmatch_num++;
        }
        if(unmatch_num > (chain_num - bloom->hash_num)){
            return 0; //quick path for check false
        }
    }
    if(match_num >= bloom->hash_num)
    {
        TIME_DIFF();
        return 1;
    }
    TIME_DIFF();
    return 0;
}
void AP_bloom_free(struct AP_bloom *bloom)
{
    for(int i=0; i<bloom->chain_num; i++)
    {
        ap_slice_chain_free(bloom, bloom->heads[i]);
        bloom->heads[i]=NULL;
    }
    APMOOL_FREE(bloom->mp, bloom->heads);

#if APBM_USE_MEMPOOL
    apbm_mempool_destroy(bloom->mp);
#endif
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
size_t AP_bloom_mem_blocks(const struct AP_bloom *bloom)
{
    size_t blocks=0;
    blocks++;
    blocks += bloom->chain_num;
    for(int i=0; i<bloom->chain_num; i++)
    {
        blocks += ap_slice_chain_mem_blocks(bloom->heads[i]);
    }
    return blocks;
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
        bloom->heads[i]=ap_slice_chain_deserialize(bloom, blob+offset, blob_sz-offset);
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
                ap_slice_chain_free(dst, dst->heads[i]);
            }
            
            memcpy(dst, src, offsetof(struct AP_bloom, heads));
            free(dst->heads);
            dst->heads = ALLOC(struct ap_slice*, dst->chain_num);
            for(int i=0; i<dst->chain_num; i++)
            {
                dst->heads[i] = ap_slice_chain_duplicate(dst, src->heads[i]);
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
        ap_slice_chain_merge(dst,&(dst->heads[i]), src->heads[i]);
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

size_t AP_bloom_stat_get_hash_num(const struct AP_bloom *bloom)
{
    return bloom->hash_num;
}
size_t AP_bloom_stat_get_new_num(const struct AP_bloom *bloom)
{
    return bloom->stat.slice_new_num_acc;
}
size_t AP_bloom_stat_get_free_num(const struct AP_bloom *bloom)
{
    return bloom->stat.slice_free_num_acc;
}
size_t AP_bloom_stat_get_expand_num(const struct AP_bloom *bloom)
{
    return bloom->stat.expand_num_acc;
}
size_t AP_bloom_stat_get_cur_slice_num(const struct AP_bloom *bloom)
{
    return bloom->stat.slice_num_rt;
}
size_t AP_bloom_stat_get_expand_max_multiple(const struct AP_bloom *bloom)
{
    return bloom->stat.expand_max_multiple_rt;
}