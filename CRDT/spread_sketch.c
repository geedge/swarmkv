
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "xxhash.h"
#include "uthash.h"

#include "spread_sketch.h"
#include "hll_sheet.h"

#define DECAY_RATE_FACTOR 1.1 // 每隔 DECAY_RATE_FACTOR * time_window 的时间，将L 减小


struct stagger_hll_group {
    uint32_t *registers;

    int n_row;
    int n_width;
    int sheet_size;

    // shared configuration
    long long reset_idx;
    struct timeval reset_time;

    // configuration
    unsigned char precision;
    long long time_window_ms;
};

struct spread_sketch {
    int depth;
    int width;
    long long time_window_ms;

    // buckets
    unsigned char *max_levels;
    char **keys;
    struct stagger_hll_group *counters;
    long long *last_updates; // 也许用不着，先放在这儿，方案2

    long long last_decay_time; // 方案1使用
};

struct ss_query_result; // defined in spread_sketch.h

struct stagger_hll_group *shllg_new(unsigned depth, unsigned width, unsigned char precision, int time_window_s, struct timeval now) {
    struct stagger_hll_group *pthis = malloc(sizeof(struct stagger_hll_group));

    pthis->n_row = depth;
    pthis->n_width = width;
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
    printf("reset reg count: %lld\n", reset_reg_count);
    if(reset_reg_count)
    {
        hlls_reset_registers(pthis->registers, pthis->precision, pthis->sheet_size, pthis->reset_idx, reset_reg_count);
        hlls_advance_reset_index(&pthis->reset_idx, reset_reg_count, pthis->precision);
    }
}

void shllg_update(struct stagger_hll_group *pthis, int r, int w, uint64_t key) {
    int idx = r * pthis->n_width + w;
    hlls_add_hash(pthis->registers, pthis->precision, pthis->sheet_size, key, idx);
}

double shllg_count(struct stagger_hll_group *pthis, int r, int w) {
    int idx = r * pthis->n_width + w;
    return hlls_count(pthis->registers, idx, pthis->precision, pthis->sheet_size, pthis->reset_idx, pthis->time_window_ms);
}

void shllg_free(struct stagger_hll_group *pthis) {
    hlls_free_register(pthis->registers);
    free(pthis);
}

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now) {
    struct spread_sketch *pthis = malloc(sizeof(struct spread_sketch));

    pthis->depth = depth;
    pthis->width = width;
    pthis->time_window_ms = time_window_s * 1000;

    pthis->max_levels = calloc(depth * width, sizeof(unsigned char));
    pthis->keys = calloc(depth * width, sizeof(char *));
    pthis->counters = shllg_new(depth, width, precision, time_window_s, now);
    pthis->last_updates = calloc(depth * width, sizeof(long long));

    return pthis;
}

/*
hash utils:

xxhash 在输入较短的情况下比murmurhash 快（https://github.com/sebastian-software/node-hash-comparison），因此使用xxhash。

需要把src和dst 哈希到一起，但是我严重觉得只哈希dst 就行，这里可以实验一下。
anyway, 我觉得xxhash 就好
*/

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

    move_registers_forward(ss->counters, &now);

    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;

        if (DECAY_L_FLAG) { // 方案1，将L 定时decay掉一些，本质是，输入的level 是程序运行时间的函数。
            if (now_ms - ss->last_decay_time > DECAY_RATE_FACTOR * ss->time_window_ms) {
                ss->max_levels[bucket_idx] -= 1; // TODO: 这样变成指数衰减了，对超大level其实不太公平，可能level衰减是level自己的函数
            }
        } else { // 方案2，耗更多内存，记录一个更新的时间戳
            if (ss->last_updates[bucket_idx] - now_ms > ss->time_window_ms) {
                ss->max_levels[bucket_idx] = 0;
            }
        }

        bool k_same_flag = (ss->keys[bucket_idx] != NULL) && (strcmp(ss->keys[bucket_idx], key_main) == 0);
        if (k_same_flag) {
            ss->last_updates[bucket_idx] = now_ms;
        }

        if (ss->max_levels[bucket_idx] < level) {
            ss->max_levels[bucket_idx] = level;
            if (!k_same_flag) {
                free(ss->keys[bucket_idx]);
                ss->keys[bucket_idx] = strdup(key_main);
                ss->last_updates[bucket_idx] = now_ms;
            }
        }

        shllg_update(ss->counters, i, bucket_idx, hash_y);
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

        double est = shllg_count(ss->counters, i, bucket_idx);
        if (est < count_min) {
            count_min = est;
        }
    }
    return count_min;
}

void spread_sketch_query(struct spread_sketch *ss, struct timeval now, struct ss_query_result **results, int *n_results) {
    // TODO: 复制hlls再查询，入参是const

    struct stagger_hll_group *hlls = ss->counters;
    if (hlls_should_slide(hlls->precision, hlls->time_window_ms, now, hlls->reset_time)) {
        move_registers_forward(hlls, &now);
    }

    // find out all unique keys
    struct key_item {
        char *key;
        UT_hash_handle hh;
    } *key_counts = NULL;

    for (int i = 0; i < ss->depth; i++) {
        for (int j = 0; j < ss->width; j++) {
            char *key = ss->keys[i * ss->width + j];
            if (key == NULL) {
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
    struct ss_query_result *ret = malloc(sizeof(struct ss_query_result) * HASH_COUNT(key_counts));

    int n_results_found = 0;
    struct key_item *kc, *tmp;
    HASH_ITER(hh, key_counts, kc, tmp) {
        double count = spread_sketch_point_query(ss, kc->key);
        // printf("key: %s, count: %f\n", kc->key, count);
        if (count > 1.0) {
            printf("key: %s, count: %f\n", kc->key, count);
            ret[n_results_found].key = strdup(kc->key);
            ret[n_results_found].count = count;
            n_results_found++;
        }
    }

    *results = ret;
    *n_results = n_results_found;

    HASH_ITER(hh, key_counts, kc, tmp) {
        HASH_DEL(key_counts, kc);
        free(kc);
    }
}

void spread_sketch_free(struct spread_sketch *ss) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        free(ss->keys[i]);
    }
    free(ss->keys);
    free(ss->max_levels);
    shllg_free(ss->counters);
    free(ss->last_updates);
    free(ss);
}



void merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
}

void serialize(struct spread_sketch *ss, char **blob, size_t *blob_sz)
{
}

struct spread_sketch *deserialize(const char *blob, size_t blob_sz)
{
}

void spread_sketch_merge_blob(struct spread_sketch *dst, const char *blob, size_t blob_sz)
{
}

