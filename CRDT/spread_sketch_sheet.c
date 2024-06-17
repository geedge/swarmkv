
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>   
#include <assert.h>

#include "xxhash.h"
#include "uthash.h"

#include "spread_sketch.h"
#include "hll_sheet.h"
#include "crdt_utils.h"



struct stagger_hll_group {
    uint32_t *registers;

    // shared configuration
    long long reset_idx;
    struct timeval reset_time;

    // configuration
    unsigned char precision;
    long long time_window_ms;
    int sheet_size;
};

struct spread_sketch {
    int depth;
    int width;
    long long time_window_ms;

    // buckets
    // TODO: 定义一个bucket 结构体
    unsigned char *max_levels;
    char **keys; // TODO: 不一定是字符串
    struct stagger_hll_group *counters; // TODO: 先改成简单版本
    long long *last_updates_ms; // linux timestamp since Jan 1st 1970
};

struct ss_query_result; // defined in spread_sketch.h

struct stagger_hll_group *shllg_new(unsigned depth, unsigned width, unsigned char precision, int time_window_s, struct timeval now) {
    struct stagger_hll_group *pthis = malloc(sizeof(struct stagger_hll_group));

    pthis->sheet_size = depth * width;

    pthis->precision = precision;
    pthis->time_window_ms = time_window_s * 1000;

    pthis->reset_idx = 0;
    pthis->reset_time = now;

    pthis->registers = hlls_create_register(precision, pthis->sheet_size);

    return pthis;
}

void move_registers_forward(struct stagger_hll_group *pthis, const struct timeval *now) {
    long long reset_reg_count = hlls_get_reset_register_count(pthis->precision, pthis->time_window_ms, *now, &pthis->reset_time);
    if(reset_reg_count)
    {
        hlls_reset_registers(pthis->registers, pthis->precision, pthis->sheet_size, pthis->reset_idx, reset_reg_count);
        hlls_advance_reset_index(&pthis->reset_idx, reset_reg_count, pthis->precision);
    }
}

void shllg_update(struct stagger_hll_group *pthis, int bucket_idx, uint64_t key) {
    hlls_add_hash(pthis->registers, pthis->precision, pthis->sheet_size, key, bucket_idx);
}

double shllg_count(struct stagger_hll_group *pthis, int bucket_idx) {
    return hlls_count(pthis->registers, bucket_idx, pthis->precision, pthis->sheet_size, pthis->reset_idx, pthis->time_window_ms);
}

void shllg_free(struct stagger_hll_group *pthis) {
    hlls_free_register(pthis->registers);
    free(pthis);
}

size_t shllg_size(unsigned char precision, int sheet_size) {
    return hlls_size(precision, sheet_size) + sizeof(struct stagger_hll_group);
}

struct stagger_hll_group *shllg_duplicate(const struct stagger_hll_group *src) {
    struct stagger_hll_group *pthis = malloc(sizeof(struct stagger_hll_group));
    pthis->sheet_size = src->sheet_size;

    pthis->precision = src->precision;
    pthis->time_window_ms = src->time_window_ms;

    pthis->reset_idx = src->reset_idx;
    pthis->reset_time = src->reset_time;

    pthis->registers = hlls_duplicate(src->registers, src->precision, src->sheet_size);

    return pthis;
}

void shllg_merge(struct stagger_hll_group *dst, const struct stagger_hll_group *src) {
    hlls_merge(dst->registers, src->registers, dst->precision, dst->sheet_size);
}

void shllg_serialize(struct stagger_hll_group *pthis, char **blob, size_t *blob_sz) {
    struct serializer_reader *reader = serializer_reader_new();
    serializer_reader_read_uint(reader, pthis->precision);
    serializer_reader_read_uint(reader, pthis->sheet_size);
    serializer_reader_read_longlong(reader, pthis->time_window_ms);

    serializer_reader_read_longlong(reader, pthis->reset_idx);
    serializer_reader_read_longlong(reader, pthis->reset_time.tv_sec);
    serializer_reader_read_longlong(reader, pthis->reset_time.tv_usec);

    serializer_reader_read_bin(reader, (const char *)pthis->registers, hlls_size(pthis->precision, pthis->sheet_size));
    serializer_reader_finalize(reader, blob, blob_sz);
}

struct stagger_hll_group *shllg_deserialize(const char *blob, size_t blob_sz) {
    struct serializer_writer *writer = serializer_writer_new(blob, blob_sz);
    struct stagger_hll_group *pthis = malloc(sizeof(struct stagger_hll_group));
    int ret;

    uint32_t precision_tmp;
    ret = serializer_writer_write_uint(writer, &precision_tmp);
    assert(ret != -1);
    pthis->precision = (unsigned char)precision_tmp;

    uint32_t sheet_size_tmp;
    ret = serializer_writer_write_uint(writer, &sheet_size_tmp);
    assert(ret != -1);
    pthis->sheet_size = sheet_size_tmp;
    ret = serializer_writer_write_longlong(writer, &pthis->time_window_ms);
    assert(ret != -1);

    ret = serializer_writer_write_longlong(writer, &pthis->reset_idx);
    assert(ret != -1);
    long long tv_sec_tmp, tv_usec_tmp;
    ret = serializer_writer_write_longlong(writer, &tv_sec_tmp);
    assert(ret != -1);
    ret = serializer_writer_write_longlong(writer, &tv_usec_tmp);
    assert(ret != -1);
    pthis->reset_time.tv_sec = tv_sec_tmp;
    pthis->reset_time.tv_usec = tv_usec_tmp;

    char *registers = NULL;
    size_t registers_sz = 0;
    ret = serializer_writer_write_bin(writer, &registers, &registers_sz);
    assert(ret != -1);
    assert(registers_sz == hlls_size(pthis->precision, pthis->sheet_size));

    pthis->registers = (uint32_t *)registers;

    serializer_writer_free(writer);
    
    return pthis;
}

/* -------------------------------------------------------------------------- */
/*                                spread_sketch                               */
/* -------------------------------------------------------------------------- */

unsigned char cal_true_level(const struct spread_sketch *ss, int bucket_idx, long long t) {
    /*
    return f(t), the actual level of bucket, which satisfy:
    1. d 2^f(t)/dt is constant
    2.  f(t0 + T) = 1
    3. f((t0) = L )
    */
    unsigned L = ss->max_levels[bucket_idx];
    long long T = ss->time_window_ms;
    long long t0 = ss->last_updates_ms[bucket_idx];

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

    return (unsigned char)(log2(a * ((double)(t-t0)) + b) + 0.5);
}

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now) {
    struct spread_sketch *pthis = malloc(sizeof(struct spread_sketch));

    pthis->depth = depth;
    pthis->width = width;
    pthis->time_window_ms = time_window_s * 1000;

    pthis->max_levels = calloc(depth * width, sizeof(unsigned char));
    pthis->keys = calloc(depth * width, sizeof(char *));
    pthis->counters = shllg_new(depth, width, precision, time_window_s, now);
    pthis->last_updates_ms = calloc(depth * width, sizeof(long long));

    return pthis;
}

/*
hash utils:

xxhash 在输入较短的情况下比murmurhash 快（https://github.com/sebastian-software/node-hash-comparison），因此使用xxhash。

需要把src和dst 哈希到一起，但是我严重觉得只哈希dst 就行，这里可以实验一下。
anyway, 我觉得xxhash 就好
*/

void spread_sketch_update(struct spread_sketch *ss, const char *key_main, size_t key_main_length, const char *key_spread, size_t key_spread_length, struct timeval now) {
    uint64_t hash_y = XXH3_64bits_withSeed(key_spread, strlen(key_spread), 171);
    unsigned char level = (unsigned char)__builtin_clzll(hash_y) + 1;
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf 
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key_main, strlen(key_main), 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    move_registers_forward(ss->counters, &now);

    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;


        if ((ss->keys[bucket_idx] != NULL) && (strcmp(ss->keys[bucket_idx], key_main) == 0)) {
            ss->last_updates_ms[bucket_idx] = now_ms;
            if (ss->max_levels[bucket_idx] < level) {
                ss->max_levels[bucket_idx] = level;
            }
        } else {
            unsigned char true_level;
            if (ss->keys[bucket_idx] == NULL) {
                true_level = 0;
            } else {
                true_level = cal_true_level(ss, bucket_idx, now_ms);
            }

            if (true_level < level) {
                free(ss->keys[bucket_idx]);
                ss->keys[bucket_idx] = strdup(key_main);
                ss->last_updates_ms[bucket_idx] = now_ms;
                ss->max_levels[bucket_idx] = level;
            }
        }

        shllg_update(ss->counters, bucket_idx, hash_y);
    }
}

double spread_sketch_point_query(struct spread_sketch *ss, const char *key_main) {
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key_main, strlen(key_main), 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    double count_min = (double)INT32_MAX;
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;

        double est = shllg_count(ss->counters, bucket_idx);
        if (est < count_min) {
            count_min = est;
        }
    }
    return count_min;
}

void ss_query_result_free(struct ss_query_result *result) {
    if (result == NULL) {
        return;
    }

    for(int i=0; i<result->n_results; i++){
        free(result->key[i]);
    }
    free(result->key);
    free(result->count);
    free(result);
}


struct ss_query_result *spread_sketch_query(const struct spread_sketch *ss, struct timeval now) {
    // gen a duplication, so that step won't change the original
    struct spread_sketch *duplicate = malloc(sizeof(struct spread_sketch));
    duplicate->depth = ss->depth;
    duplicate->width = ss->width;
    duplicate->time_window_ms = ss->time_window_ms;
    duplicate->max_levels = NULL; // `max level` is useless in query
    duplicate->keys = ss->keys; // keys are read only
    duplicate->counters = shllg_duplicate(ss->counters); //  counters are deep copy

    struct stagger_hll_group *hlls = duplicate->counters;
    if (hlls_should_slide(hlls->precision, hlls->time_window_ms, now, hlls->reset_time)) {
        move_registers_forward(hlls, &now);
    }

    // find out all unique keys
    struct key_item {
        char *key;
        UT_hash_handle hh;
    } *key_counts = NULL;

    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    for (int i = 0; i < ss->depth; i++) {
        for (int j = 0; j < ss->width; j++) {
            char *key = ss->keys[i * ss->width + j];
            if (key == NULL) {
                continue;
            }
            if (now_ms - ss->last_updates_ms[i * ss->width + j] > ss->time_window_ms) {
                printf("skip because the key is not updated for a long time, key: %s, last update: %lld, now: %lld, time window: %lld\n", key, ss->last_updates_ms[i * ss->width + j], now_ms, ss->time_window_ms);
                continue;
            }

            struct key_item *kc;
            HASH_FIND_STR(key_counts, key, kc);
            if (kc == NULL) {
                kc = malloc(sizeof(struct key_item));
                kc->key = key;
                HASH_ADD_KEYPTR(hh, key_counts, kc->key, strlen(kc->key), kc);
            }
        }
    }

    printf("find number of keys: %d\n", HASH_COUNT(key_counts));
    // query
    int n_result = HASH_COUNT(key_counts);
    struct ss_query_result *ret = malloc(sizeof(struct ss_query_result));
    ret->key = malloc(sizeof(char *) * n_result);
    ret->count = malloc(sizeof(double) * n_result);

    int n_results_found = 0;
    struct key_item *kc, *tmp;
    HASH_ITER(hh, key_counts, kc, tmp) {
        double count = spread_sketch_point_query(duplicate, kc->key);
        if (count >= 1.0) {
            ret->key[n_results_found] = strdup(kc->key);
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
    shllg_free(duplicate->counters);
    free(duplicate);

    return ret;
}

void spread_sketch_free(struct spread_sketch *ss) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        free(ss->keys[i]);
    }
    free(ss->keys);
    free(ss->max_levels);
    shllg_free(ss->counters);
    free(ss->last_updates_ms);
    free(ss);
}

void spread_sketch_merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
    assert(dst->depth == src->depth && dst->width == src->width);
    assert(dst->time_window_ms == src->time_window_ms);
    assert(dst->counters->precision == src->counters->precision);

    for (int i = 0; i < dst->depth * dst->width; i++) {
        if (src->keys[i] == NULL) {
            continue;
        }

        if (dst->keys[i] == NULL) {
            dst->keys[i] = strdup(src->keys[i]);
            dst->max_levels[i] = src->max_levels[i];
            dst->last_updates_ms[i] = src->last_updates_ms[i];
            continue;
        }

        if (strcmp(dst->keys[i], src->keys[i]) == 0) {
            if (src->max_levels[i] > dst->max_levels[i]) {
                dst->max_levels[i] = src->max_levels[i];
            }
            if (src->last_updates_ms[i] > dst->last_updates_ms[i]) {
                dst->last_updates_ms[i] = src->last_updates_ms[i];
            }
        } else {
            unsigned char true_level_src = cal_true_level(src, i, src->last_updates_ms[i]);
            unsigned char true_level_dst = cal_true_level(dst, i, dst->last_updates_ms[i]);

            if (true_level_src > true_level_dst) {
                free(dst->keys[i]);
                dst->keys[i] = strdup(src->keys[i]);
                dst->max_levels[i] = src->max_levels[i];
                dst->last_updates_ms[i] = src->last_updates_ms[i];
            }
        }
    }

    shllg_merge(dst->counters, src->counters);
}

void spread_sketch_serialize(const struct spread_sketch *ss, char **blob, size_t *blob_sz)
{
    struct serializer_reader *reader = serializer_reader_new();
    int bucket_length = ss->depth * ss->width;
    serializer_reader_read_uint(reader, ss->depth);
    serializer_reader_read_uint(reader, ss->width);
    serializer_reader_read_longlong(reader, ss->time_window_ms);

    for (int i = 0; i < bucket_length; i++) {
        if (ss->keys[i] == NULL) {
            serializer_reader_read_nil(reader);
        } else {
            serializer_reader_read_str(reader, ss->keys[i]);
        }
    }
    serializer_reader_read_bin(reader, (const char *)ss->max_levels, bucket_length);
    serializer_reader_read_bin(reader, (const char *)ss->last_updates_ms, bucket_length * sizeof(long long));

    char *shllg_blob = NULL;
    size_t shllg_blob_sz = 0;
    shllg_serialize(ss->counters, &shllg_blob, &shllg_blob_sz);
    serializer_reader_read_bin(reader, shllg_blob, shllg_blob_sz);
    free(shllg_blob);

    serializer_reader_finalize(reader, blob, blob_sz);
}

struct spread_sketch *spread_sketch_deserialize(const char *blob, size_t blob_sz)
{
    struct serializer_writer *writer = serializer_writer_new(blob, blob_sz);
    struct spread_sketch *ss = malloc(sizeof(struct spread_sketch));
    int ret;
    ret = serializer_writer_write_uint(writer, (uint32_t*)&ss->depth);
    assert(ret != -1);
    ret = serializer_writer_write_uint(writer, (uint32_t*)&ss->width);
    assert(ret != -1);
    ret = serializer_writer_write_longlong(writer, &ss->time_window_ms);
    assert(ret != -1);

    int bucket_length = ss->depth * ss->width;

    ss->keys = calloc(bucket_length, sizeof(char *));
    for (int i = 0; i < bucket_length; i++) {
        ret = serializer_writer_expect_nil(writer);
        assert(ret != -1);
        if (ret == 0) {
            char *key = NULL;
            ret = serializer_writer_write_str(writer, &key);
            assert(ret != -1);
            ss->keys[i] = key;
        } else {
            ss->keys[i] = NULL;
        }
    }
    size_t size_dummy = 0;
    char *max_level_tmp = NULL;
    ret = serializer_writer_write_bin(writer, &max_level_tmp, &size_dummy);
    assert(ret != -1);
    assert(size_dummy == bucket_length * sizeof(unsigned char));
    ss->max_levels = (unsigned char *)max_level_tmp;
    
    char *last_update_tmp = NULL;
    ret = serializer_writer_write_bin(writer, &last_update_tmp, &size_dummy);
    assert(ret != -1);
    assert(size_dummy == bucket_length * sizeof(long long));
    ss->last_updates_ms = (long long *)last_update_tmp;

    char *shllg_blob = NULL;
    size_t shllg_blob_sz = 0;
    ret = serializer_writer_write_bin(writer, &shllg_blob, &shllg_blob_sz);
    assert(ret != -1);
    ss->counters = shllg_deserialize(shllg_blob, shllg_blob_sz);
    free(shllg_blob);

    serializer_writer_free(writer);

    return ss;
}

void spread_sketch_merge_blob(struct spread_sketch *dst, const char *blob, size_t blob_sz)
{
}

size_t spread_sketch_calculate_memory_usage(const struct spread_sketch *ss)
{
    size_t ret = 0;
    ret += sizeof(struct spread_sketch);

    size_t bucket_size = sizeof(unsigned char) + (sizeof(char *) + 5) + sizeof(long long);
    size_t average_sthllg = shllg_size(ss->counters->precision, ss->counters->sheet_size) / ss->width / ss->depth;
    bucket_size += average_sthllg;
    printf("every bucket size: %ld\n", bucket_size);

    ret += ss->depth * ss->width * bucket_size;
    return ret;
}
