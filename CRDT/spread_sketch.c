
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
    unsigned char level;
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

struct ss_query_result; // defined in spread_sketch.h


unsigned char cal_true_level(const struct spread_sketch *ss, int bucket_idx, long long t) {
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

    return (unsigned char)(log2(a * ((double)(t-t0)) + b) + 0.5);
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

void spread_sketch_update(struct spread_sketch *ss, const char *key_main, const char *key_spread, struct timeval now) {
    uint64_t hash_y = XXH3_64bits_withSeed(key_spread, strlen(key_spread), 171);
    unsigned char level = (unsigned char)__builtin_clzll(hash_y) + 1;
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;
    
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(key_main, strlen(key_main), 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    move_registers_forward(ss, &now);

    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;
        struct bucket *bucket = &ss->buckets[bucket_idx];

        if (bucket->key != NULL && (strcmp(bucket->key, key_main) == 0)) {
            bucket->last_update_ms = now_ms;
            if (bucket->level < level) {
                bucket->level = level;
            }
        } else {
            unsigned char true_level;
            if (bucket->key == NULL) {
                true_level = 0;
            } else {
                true_level = cal_true_level(ss, bucket_idx, now_ms);
            }

            if (true_level < level) {
                free(bucket->key);
                bucket->key = strdup(key_main);
                bucket->last_update_ms = now_ms;
                bucket->level = level;
            }
        }

        hll_add_hash(bucket->sthll_register, ss->precision, hash_y);
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

        double est = hll_count(ss->buckets[bucket_idx].sthll_register, ss->precision, ss->reset_idx, ss->time_window_ms);
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
    // find out all unique keys
    struct key_item {
        char *key;
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
            HASH_FIND_STR(key_counts, key, kc);
            if (kc == NULL) {
                kc = malloc(sizeof(struct key_item));
                kc->key = key;
                HASH_ADD_KEYPTR(hh, key_counts, kc->key, strlen(kc->key), kc);
            }
        }
    }

    // query
    int n_result = HASH_COUNT(key_counts);
    struct ss_query_result *ret = malloc(sizeof(struct ss_query_result));
    if (n_result == 0) {
        ret->n_results = 0;
        ret->key = NULL;
        ret->count = NULL;
        return ret;
    }
    ret->key = malloc(sizeof(char *) * n_result);
    ret->count = malloc(sizeof(double) * n_result);

    // gen a duplication, so that step won't change the original
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

    if (hll_should_slide(ss->precision, ss->time_window_ms, now, ss->reset_time)) {
        move_registers_forward(duplicate, &now);
    }

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
    
    for (int i = 0; i < ss->depth * ss->width; i++) {
        hll_free_register(duplicate->buckets[i].sthll_register);
    }
    free(duplicate->buckets);
    free(duplicate);

    return ret;
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
            bucket_dst->key = strdup(bucket_src->key);
            bucket_dst->key_len = bucket_src->key_len;
            bucket_dst->level = bucket_src->level;
            bucket_dst->last_update_ms = bucket_src->last_update_ms;

            bucket_dst->sthll_register = hll_duplicate(bucket_src->sthll_register, dst->precision);
            continue;
        }

        if (strcmp(bucket_dst->key, bucket_src->key) == 0) {
            if (bucket_src->level > bucket_dst->level) {
                bucket_dst->level = bucket_src->level;
            }
            if (bucket_src->last_update_ms > bucket_dst->last_update_ms) {
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        } else {
            unsigned char true_level_src = cal_true_level(src, i, bucket_src->last_update_ms);
            unsigned char true_level_dst = cal_true_level(dst, i, bucket_dst->last_update_ms);

            if (true_level_src > true_level_dst) {
                free(bucket_dst->key);
                bucket_dst->key = strdup(bucket_src->key);
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
    struct serializer_reader *reader = serializer_reader_new();
    serializer_reader_read_uint(reader, ss->depth);
    serializer_reader_read_uint(reader, ss->width);
    serializer_reader_read_longlong(reader, ss->time_window_ms);
    serializer_reader_read_uint(reader, ss->precision);

    serializer_reader_read_longlong(reader, ss->reset_idx);
    serializer_reader_read_longlong(reader, ss->reset_time.tv_sec);
    serializer_reader_read_longlong(reader, ss->reset_time.tv_usec);

    for (int i = 0; i < ss->depth * ss->width; i++) {
        const struct bucket *bucket = &ss->buckets[i];
        serializer_reader_read_uint(reader, bucket->level);
        serializer_reader_read_longlong(reader, bucket->last_update_ms);
        if (bucket->key != NULL) {
            serializer_reader_read_str(reader, bucket->key);
        } else {
            serializer_reader_read_nil(reader);
        }

        serializer_reader_read_bin(reader, (const char *)bucket->sthll_register, hll_size(ss->precision));
    }

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
    uint32_t precision_tmp;
    ret = serializer_writer_write_uint(writer, &precision_tmp);
    assert(ret != -1);
    ss->precision = (unsigned char)precision_tmp;
    ret = serializer_writer_write_longlong(writer, &ss->reset_idx);
    assert(ret != -1);
    ret = serializer_writer_write_longlong(writer, &ss->reset_time.tv_sec);
    assert(ret != -1);
    ret = serializer_writer_write_longlong(writer, &ss->reset_time.tv_usec);
    assert(ret != -1);

    ss->buckets = calloc(ss->depth * ss->width, sizeof(struct bucket));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        struct bucket *bucket = &ss->buckets[i];
        ret = serializer_writer_write_uint(writer, &bucket->level);
        assert(ret != -1);
        ret = serializer_writer_write_longlong(writer, &bucket->last_update_ms);
        assert(ret != -1);

        ret = serializer_writer_expect_nil(writer);
        assert(ret != -1);
        if (ret == 1) {
            bucket->key = NULL;
        } else {
            char *key;
            ret = serializer_writer_write_str(writer, &key);
            assert(ret != -1);
            bucket->key = key;
            bucket->key_len = strlen(key);
        }

        size_t dummy;
        ret = serializer_writer_write_bin(writer, (char **)&bucket->sthll_register, &dummy);
        assert(ret != -1);
        assert(dummy == hll_size(ss->precision));
    }

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

    size_t bucket_size = sizeof(struct bucket) + hll_size(ss->precision) + 5; // 5 key length in average by assumption

    printf("every bucket size: %ld\n", bucket_size);

    ret += ss->depth * ss->width * bucket_size;
    return ret;
}
