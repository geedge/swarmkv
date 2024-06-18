
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>   
#include <assert.h>

#include "xxhash.h"
#include "uthash.h"

#include "spread_sketch.h"
#include "hll_common.h"
#include "crdt_utils.h"



struct bucket {
    char *key;
    uint32_t key_len;
    uint32_t level;
    long long last_update_ms; // linux timestamp since Jan 1st 1970
    uint32_t *sthll_register;
};

struct spread_sketch {
    int depth;
    int width;
    long long time_window_ms;
    unsigned char precision;

    // shared states of all sthlls
    long long reset_idx;
    struct timeval reset_time;

    struct bucket *buckets;
};

struct spread_sketch_list; // defined in spread_sketch.h


uint32_t cal_true_level(const struct spread_sketch *ss, int bucket_idx, long long t) {
    /*
    return f(t), the actual level of bucket, which satisfy:
    1. d 2^f(t)/dt is constants
    2.  f(t0 + T) = 1
    3. f((t0) = L )
    */
   struct bucket *bucket = &ss->buckets[bucket_idx];
    unsigned L = bucket->level;
    long long T = ss->time_window_ms;
    long long t0 = bucket->last_update_ms;

    assert(t >= t0);
    if (t - t0 >= T) {
        return 1;
    }
    if (L <= 1) {
        return L;
    }

    long long tmp_exp = 1 << L;
    double a = ((double)(2 - tmp_exp)) / ((double)T);
    double b = (double)(tmp_exp);

    return (uint32_t)(log2(a * ((double)(t-t0)) + b) + 0.5);
}

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now) {
    struct spread_sketch *pthis = malloc(sizeof(struct spread_sketch));

    pthis->depth = depth;
    pthis->width = width;
    pthis->time_window_ms = time_window_s * 1000;
    pthis->precision = precision;

    pthis->reset_idx = 0;
    pthis->reset_time = now;

    pthis->buckets = calloc(depth * width, sizeof(struct bucket));
    for (int i = 0; i < depth * width; i++) {
        pthis->buckets[i].sthll_register = hll_create_register(precision);
    }

    return pthis;
}

void move_registers_forward(struct spread_sketch *ss, struct timeval *now) {
    long long reset_reg_count = hll_get_reset_register_count(ss->precision, ss->time_window_ms, *now, &ss->reset_time);
    if (reset_reg_count > 0) {
        for (int i = 0; i < ss->depth * ss->width; i++) {
            hll_reset_registers(ss->buckets[i].sthll_register, ss->precision, ss->reset_idx, reset_reg_count);
        }
        hll_advance_reset_index(&ss->reset_idx, reset_reg_count, ss->precision);
    }
}

static inline bool key_equal(const char *key1, size_t key1_len, const char *key2, size_t key2_len) {
    if (key1_len != key2_len) {
        return false;
    }
    return memcmp(key1, key2, key1_len) == 0;
}

static inline char *key_dup(const char *key, size_t key_len) {
    char *ret = malloc(key_len);
    memcpy(ret, key, key_len);
    return ret;
}

double spread_sketch_add(struct spread_sketch *ss, const char *key, size_t key_length, const char *identifier, size_t identifier_length, struct timeval now) {
    uint64_t hash_y = XXH3_64bits_withSeed(identifier, identifier_length, 171);
    uint32_t level = (uint32_t)__builtin_clzll(hash_y) + 1;
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key, key_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    move_registers_forward(ss, &now);

    double min_est = (double)INT32_MAX;
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;
        struct bucket *bucket = &ss->buckets[bucket_idx];

        if (bucket->key != NULL && key_equal(bucket->key, bucket->key_len, key, key_length)) {
            bucket->last_update_ms = now_ms;
            if (bucket->level < level) {
                bucket->level = level;
            }
        } else {
            uint32_t true_level;
            if (bucket->key == NULL) {
                true_level = 0;
            } else {
                true_level = cal_true_level(ss, bucket_idx, now_ms);
            }

            if (true_level < level) {
                free(bucket->key);
                bucket->key = key_dup(key, key_length);
                bucket->key_len = key_length;
                bucket->last_update_ms = now_ms;
                bucket->level = level;
            }
        }

        hll_add_hash(bucket->sthll_register, ss->precision, hash_y);
        double est = hll_count(bucket->sthll_register, ss->precision, ss->reset_idx, ss->time_window_ms);
        if (est < min_est) {
            min_est = est;
        }
    }

    return min_est;
}

double spread_sketch_point_query(const struct spread_sketch *ss, const char *key, size_t key_length) {
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key, key_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    double count_min = (double)INT32_MAX;
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;

        double est = hll_count(ss->buckets[bucket_idx].sthll_register, ss->precision, ss->reset_idx, ss->time_window_ms);
        if (est < count_min) {
            count_min = est;
        } 
    }
    return count_min;
}

void ss_query_result_free(struct spread_sketch_list *result) {
    if (result == NULL) {
        return;
    }

    for(int i=0; i<result->n_results; i++){
        free(result->key[i]);
    }
    free(result->key);
    free(result->count);
    free(result->key_len);
    free(result);
}

struct spread_sketch *duplicate_and_step(const struct spread_sketch *ss, struct timeval *now) {
    struct spread_sketch *duplicate = malloc(sizeof(struct spread_sketch));
    duplicate->depth = ss->depth;
    duplicate->width = ss->width;
    duplicate->precision = ss->precision;
    duplicate->reset_idx = ss->reset_idx;
    duplicate->reset_time = ss->reset_time;
    duplicate->time_window_ms = ss->time_window_ms;
    duplicate->buckets = calloc(ss->depth * ss->width, sizeof(struct bucket));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        duplicate->buckets[i].sthll_register = hll_duplicate(ss->buckets[i].sthll_register, ss->precision);
    }

    move_registers_forward(duplicate, now);
    return duplicate;
}

struct spread_sketch_list *spread_sketch_list(const struct spread_sketch *ss, struct timeval now) {
    // find out all unique keys
    struct key_item {
        char *key;
        uint32_t key_len;
        UT_hash_handle hh;
    } *key_counts = NULL;

    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    for (int i = 0; i < ss->depth; i++) {
        for (int j = 0; j < ss->width; j++) {
            struct bucket *bucket = &ss->buckets[i * ss->width + j];
            char *key = bucket->key;
            if (key == NULL) {
                continue;
            }

            if (now_ms - bucket->last_update_ms > ss->time_window_ms) {
                continue;
            }

            struct key_item *kc;
            HASH_FIND(hh, key_counts, key, bucket->key_len, kc);
            if (kc == NULL) {
                kc = malloc(sizeof(struct key_item));
                kc->key = key;
                kc->key_len = bucket->key_len;
                HASH_ADD_KEYPTR(hh, key_counts, kc->key, bucket->key_len, kc);
            }
        }
    }

    // query
    int n_result = HASH_COUNT(key_counts);
    struct spread_sketch_list *ret = malloc(sizeof(struct spread_sketch_list));
    if (n_result == 0) {
        ret->n_results = 0;
        ret->key = NULL;
        ret->count = NULL;
        return ret;
    }
    ret->key = malloc(sizeof(char *) * n_result);
    ret->count = malloc(sizeof(double) * n_result);
    ret->key_len = malloc(sizeof(size_t) * n_result);

    struct spread_sketch *using_ss = (struct spread_sketch *)ss;
    if (hll_should_slide(ss->precision, ss->time_window_ms, now, ss->reset_time)) {
        using_ss = duplicate_and_step(ss, &now);
    }

    int n_results_found = 0;
    struct key_item *kc, *tmp;
    HASH_ITER(hh, key_counts, kc, tmp) {
        double count = spread_sketch_point_query(using_ss, kc->key, kc->key_len);
        if (count >= 1.0) {
            ret->key[n_results_found] = key_dup(kc->key, kc->key_len);
            ret->key_len[n_results_found] = kc->key_len;
            ret->count[n_results_found] = count;
            n_results_found++;
        }
    }
    ret->n_results = n_results_found;

    // clean up
    HASH_ITER(hh, key_counts, kc, tmp) {
        HASH_DEL(key_counts, kc);
        free(kc);
    }
    
    if (using_ss != ss) {
        spread_sketch_free(using_ss);
    }

    return ret;
}

double spread_sketch_query(const struct spread_sketch *ss, const char *key, size_t key_length, struct timeval now) {    
    if (hll_should_slide(ss->precision, ss->time_window_ms, now, ss->reset_time)) {
        struct spread_sketch *duplicate = duplicate_and_step(ss, &now);
        double ret = spread_sketch_point_query(duplicate, key, key_length);
        spread_sketch_free(duplicate);
        return ret;
    } else {
        return spread_sketch_point_query(ss, key, key_length);
    }
}

void spread_sketch_free(struct spread_sketch *ss) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        free(ss->buckets[i].key);
        hll_free_register(ss->buckets[i].sthll_register);
    }
    free(ss->buckets);
    free(ss);
}

void spread_sketch_merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
    assert(dst->depth == src->depth && dst->width == src->width);
    assert(dst->time_window_ms == src->time_window_ms);
    assert(dst->precision == src->precision);

    for (int i = 0; i < dst->depth * dst->width; i++) {
        const struct bucket *bucket_src = &src->buckets[i];
        struct bucket *bucket_dst = &dst->buckets[i];

        if (bucket_src->key == NULL) {
            continue;
        }

        if (bucket_dst->key == NULL) {
            bucket_dst->key = key_dup(bucket_src->key, bucket_src->key_len);
            bucket_dst->key_len = bucket_src->key_len;
            bucket_dst->level = bucket_src->level;
            bucket_dst->last_update_ms = bucket_src->last_update_ms;

            bucket_dst->sthll_register = hll_duplicate(bucket_src->sthll_register, dst->precision);
            continue;
        }

        if (key_equal(bucket_src->key, bucket_src->key_len, bucket_dst->key, bucket_dst->key_len)) {
            if (bucket_src->level > bucket_dst->level) {
                bucket_dst->level = bucket_src->level;
            }
            if (bucket_src->last_update_ms > bucket_dst->last_update_ms) {
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        } else {
            uint32_t true_level_src = cal_true_level(src, i, bucket_src->last_update_ms);
            uint32_t true_level_dst = cal_true_level(dst, i, bucket_dst->last_update_ms);

            if (true_level_src > true_level_dst) {
                free(bucket_dst->key);
                bucket_dst->key = key_dup(bucket_src->key, bucket_src->key_len);
                bucket_dst->key_len = bucket_src->key_len;
                bucket_dst->level = bucket_src->level;
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        }

        hll_merge(bucket_dst->sthll_register, src->buckets[i].sthll_register, dst->precision);
    }
}

void spread_sketch_serialize(const struct spread_sketch *ss, char **blob, size_t *blob_sz)
{
    // get serialize size 
    size_t sz = 0;
    sz += sizeof(struct spread_sketch);
    sz += ss->depth * ss->width * (sizeof(struct bucket) + hll_size(ss->precision));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        sz += ss->buckets[i].key_len;
    }

    char *buffer = malloc(sz);
    *blob = buffer;
    *blob_sz = sz;

    memcpy(buffer, ss, sizeof(struct spread_sketch));
    buffer += sizeof(struct spread_sketch);
    for (int i = 0; i < ss->depth * ss->width; i++) {
        const struct bucket *bucket = &ss->buckets[i];
        memcpy(buffer, bucket, sizeof(struct bucket));
        buffer += sizeof(struct bucket);
        memcpy(buffer, bucket->sthll_register, hll_size(ss->precision));
        buffer += hll_size(ss->precision);
        if (bucket->key != NULL) {
            memcpy(buffer, bucket->key, bucket->key_len);
            buffer += bucket->key_len;
        }
    }
}

struct spread_sketch *spread_sketch_deserialize(const char *blob, size_t blob_sz)
{
    struct spread_sketch *ss = malloc(sizeof(struct spread_sketch));
    memcpy(ss, blob, sizeof(struct spread_sketch));
    blob += sizeof(struct spread_sketch);
    blob_sz -= sizeof(struct spread_sketch);

    ss->buckets = calloc(ss->depth * ss->width, sizeof(struct bucket));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        struct bucket *bucket = &ss->buckets[i];

        memcpy(bucket, blob, sizeof(struct bucket));
        blob += sizeof(struct bucket);

        bucket->sthll_register = (uint32_t *)hll_create_register(ss->precision);
        memcpy(bucket->sthll_register, blob, hll_size(ss->precision));
        blob += hll_size(ss->precision);

        if (bucket->key_len > 0) {
            bucket->key = malloc(bucket->key_len);
            memcpy(bucket->key, blob, bucket->key_len);
            blob += bucket->key_len;
        }
    }

    return ss;
}

void spread_sketch_merge_blob(struct spread_sketch *dst, const char *blob, size_t blob_sz)
{
    struct spread_sketch *src = spread_sketch_deserialize(blob, blob_sz);
    spread_sketch_merge(dst, src);
    spread_sketch_free(src);
}

size_t spread_sketch_calculate_memory_usage(const struct spread_sketch *ss)
{
    size_t ret = 0;
    ret += sizeof(struct spread_sketch);

    size_t bucket_size = sizeof(struct bucket) + hll_size(ss->precision) + 5; // 5 key length in average by assumption

    printf("every bucket size: %ld\n", bucket_size);

    ret += ss->depth * ss->width * bucket_size;
    return ret;
}

void get_parameter_recommendation(int max_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out)
{
    int logk = max_super_spreader_number >= 3200 ? 4 : 3; // lg3200 = 3.51,round up to 4
    *depth_out = logk;

    int w;
    if (max_super_spreader_number <= 100) {
        w = max_super_spreader_number * 3 /2; // * 1.5, when the number is small, we need more width
    } else {
        w = max_super_spreader_number + 50; // + 50: 100*1.5-100 = 50, make w=f(k) continuous
    }
    if (w < 40) {
        w = 40;
    }
    *width_out = w;

    *precision_out = 6;
}