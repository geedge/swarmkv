/*
 * The HyperLogLog uses 6 bits for register, and a 64bit hash function. 
 * For our needs, we always use a dense representation and avoid the sparse/dense conversions.
 *
 */
#include "st_hyperloglog.h"
#include "hll_common.h"
#include "xxhash.h"
#include "mpack.h"
#include "crdt_utils.h"

#include <stdint.h>
#include <assert.h>


struct ST_HLL_configuration
{
    unsigned char precision;
    unsigned char pad[7];
    long long time_window_ms;
    struct timeval last_cfg;
};
struct ST_hyperloglog
{
    struct ST_HLL_configuration cfg;
    long long reset_idx;
    struct timeval reset_time;
    uint32_t *registers;
};
const size_t BLOB_HDR_SIZE= offsetof(struct ST_hyperloglog, registers);


void ST_hyperloglog_step(struct ST_hyperloglog *h, struct timeval now)
{
    if(h->cfg.time_window_ms==0)
        return;
    long long reset_reg_count=hll_get_reset_register_count(h->cfg.precision, h->cfg.time_window_ms, now, &h->reset_time);
    if(reset_reg_count)
    {
        hll_reset_registers(h->registers, h->cfg.precision, h->reset_idx, reset_reg_count);
        hll_advance_reset_index(&h->reset_idx, reset_reg_count, h->cfg.precision);
    }
    return;
}
struct ST_hyperloglog *ST_hyperloglog_new(unsigned char precision, long long time_window_seconds, const struct timeval now)
{
    struct ST_hyperloglog *h=ALLOC(struct ST_hyperloglog, 1);
    // Ensure the precision is somewhat sane
    if (precision < HLL_MIN_PRECISION || precision > HLL_MAX_PRECISION)
        return NULL;

    // Store precision
    h->cfg.precision = precision;
    h->cfg.time_window_ms=time_window_seconds;
    h->cfg.last_cfg=now;
    h->reset_time=now;
    h->reset_idx=0;

    h->registers = hll_create_register(h->cfg.precision); 
    return h;
}
void ST_hyperloglog_configure(struct ST_hyperloglog *h, unsigned char precision, int time_window_seconds, const struct timeval now)
{
    h->cfg.time_window_ms=time_window_seconds;
    hll_free_register(h->registers);
    h->cfg.precision=precision;
    h->registers = hll_create_register(h->cfg.precision);
    h->cfg.last_cfg=now;
    h->reset_time=now;
    h->reset_idx=0;
    
    return;
}
void ST_hyperloglog_info(const struct ST_hyperloglog *h, struct ST_hyperloglog_info *info)
{
    info->time_window_ms=h->cfg.time_window_ms;
    info->precision=h->cfg.precision;
    info->error=ST_hyperloglog_error_for_precision(h->cfg.precision);
    return;
}
void ST_hyperloglog_free(struct ST_hyperloglog *h)
{
    hll_free_register(h->registers);
    h->registers=NULL;
    free(h);
    return;
}
#define ST_HLL_HASH_SEED    171
int ST_hyperloglog_add_hash(struct ST_hyperloglog *h, uint64_t hash, const struct timeval now)
{
    ST_hyperloglog_step(h, now);
    return hll_add_hash(h->registers, h->cfg.precision, hash);
}
int ST_hyperloglog_add(struct ST_hyperloglog *h, const char *key, size_t keylen, const struct timeval now)
{
    uint64_t hash=0;
    hash=XXH3_64bits_withSeed(key, keylen, ST_HLL_HASH_SEED);
    return ST_hyperloglog_add_hash(h, hash, now);
}
struct ST_hyperloglog *ST_hyperloglog_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
    struct ST_hyperloglog *h=ST_hyperloglog_deserialize(blob, blob_sz);
    return h;
}
struct ST_hyperloglog *ST_hyperloglog_duplicate(const struct ST_hyperloglog *origin)
{
    struct ST_hyperloglog *copy=ST_hyperloglog_new(origin->cfg.precision, origin->cfg.time_window_ms, origin->reset_time);
    copy->reset_idx=origin->reset_idx;
    copy->reset_time=origin->reset_time;
    memcpy(&copy->cfg, &origin->cfg, sizeof(struct ST_HLL_configuration));
    free(copy->registers);
    copy->registers=hll_duplicate(origin->registers, origin->cfg.precision);
    return copy;
}

//#define DEBUG
void ST_hyperloglog_merge(struct ST_hyperloglog *dst, const struct ST_hyperloglog *src)
{
    if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), <))//Last-Write-Wins
    {
        ST_hyperloglog_configure(dst, src->cfg.precision, src->cfg.time_window_ms, src->cfg.last_cfg);
    }
    else if(timercmp(&(dst->cfg.last_cfg), &(src->cfg.last_cfg), >))
    {
        return;
    }
    if(dst->cfg.precision != src->cfg.precision) return;
    struct ST_hyperloglog *tmp=ST_hyperloglog_duplicate(src);//create a copy of src for periodic reset
    if(timercmp(&(dst->reset_time), &(tmp->reset_time), <))//correct with latest timestamp  
    {
        ST_hyperloglog_step(dst, tmp->reset_time);
    }
    else
    {
        ST_hyperloglog_step(tmp, dst->reset_time);
    }
    assert(dst->reset_idx==tmp->reset_idx);
    hll_merge(dst->registers, tmp->registers, dst->cfg.precision);
    ST_hyperloglog_free(tmp);
    return;
}
size_t ST_hyperloglog_serialized_size(const struct ST_hyperloglog *h)
{
    size_t sz=0;
    sz += BLOB_HDR_SIZE;
    sz += hll_size(h->cfg.precision);
    return sz;
}
void ST_hyperloglog_serialize(const struct ST_hyperloglog *h, char **blob, size_t *blob_sz)
{
    size_t sz=0, hll_sz=0, offset=0;
    sz = ST_hyperloglog_serialized_size(h);
    
    char *buffer = ALLOC(char, sz);
    memcpy(buffer+offset, h, BLOB_HDR_SIZE);
    offset += BLOB_HDR_SIZE;
    
    hll_sz=hll_size(h->cfg.precision);
    memcpy(buffer+offset, h->registers, hll_sz);
    offset += hll_sz;
    *blob_sz=sz;
    *blob=buffer;
    return;
}
struct ST_hyperloglog *ST_hyperloglog_deserialize(const char *blob, size_t blob_sz)
{
    struct ST_hyperloglog *h=ALLOC(struct ST_hyperloglog, 1);
    size_t offset=0;
    memcpy(h, blob, BLOB_HDR_SIZE);
    offset += BLOB_HDR_SIZE;

    h->registers=hll_create_register(h->cfg.precision);
    size_t hll_sz=hll_size(h->cfg.precision);
    memcpy(h->registers, blob+offset, hll_sz);
    return h;
}
void ST_hyperloglog_merge_blob(struct ST_hyperloglog *dst, const char *blob, size_t blob_sz)
{
    struct ST_hyperloglog *src=ST_hyperloglog_deserialize(blob, blob_sz);
    ST_hyperloglog_merge(dst, src);
    ST_hyperloglog_free(src);
    return;
}

double ST_hyperloglog_count(const struct ST_hyperloglog *h, struct timeval now)
{
    
    double est=0;
    if(h->cfg.time_window_ms && hll_should_slide(h->cfg.precision, h->cfg.time_window_ms, now, h->reset_time))
    {
        struct ST_hyperloglog *h_copy=ST_hyperloglog_duplicate(h);
        ST_hyperloglog_step(h_copy, now);
        est=hll_count(h_copy->registers, h_copy->cfg.precision, h_copy->reset_idx, h_copy->cfg.time_window_ms);
        ST_hyperloglog_free(h_copy);
        assert(est>=0);
    }
    else
    {
        est=hll_count(h->registers, h->cfg.precision, h->reset_idx, h->cfg.time_window_ms);
        assert(est>=0);
    }
    return est;
}

/**
 * Computes the upper bound on variance given
 * a precision
 * @arg precision The precision to use
 * @return The expected variance in the count,
 * or zero on error.
 */
double ST_hyperloglog_error_for_precision(unsigned char precision)
{
    // Check that the error bound is sane
    if (precision < HLL_MIN_PRECISION || precision > HLL_MAX_PRECISION)
        return 0;

    return hll_error(precision);
}
size_t ST_hyperloglog_mem_size(const struct ST_hyperloglog *h)
{
    return sizeof(struct ST_hyperloglog) + hll_size(h->cfg.precision);
}