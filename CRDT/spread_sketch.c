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
#include "exdata.h"

struct entry {
    int ref_count;
    void *exdata;
    bool dying;
    char *key;
    size_t key_len;
    UT_hash_handle hh;
};

struct spread_sketch_scheme {
    exdata_new_cb new_fn;
    exdata_free_cb free_fn;
    exdata_merge_cb merge_fn;
    exdata_reset_cb reset_fn;
    exdata_copy_cb copy_fn;
};

struct entry_table {
    struct entry *entry;

    struct spread_sketch_scheme scheme;
};

struct bucket {
    struct entry *content;
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

    struct spread_sketch_scheme scheme;
    
    struct bucket *buckets;
    struct entry_table *table;

    int level0_cnt; // used to filter out dummy adding
};

static void *default_new_fn(void *arg) {
    return NULL;
}
static void default_free_fn(void *exdata) {
    return;
}
static void default_merge_fn(void *dest, void *src) {
    return;
}
static void default_reset_fn(void *exdata) {
    return;
} 
static void *default_copy_fn(void *exdata) {
    return exdata;
}

struct spread_sketch_scheme DEFAULT_SCHEME = {
    .new_fn = default_new_fn,
    .free_fn = default_free_fn,
    .merge_fn = default_merge_fn,
    .reset_fn = default_reset_fn,
    .copy_fn = default_copy_fn
};

uint32_t cal_true_level(const struct spread_sketch *ss, int bucket_idx, long long t) {
    /*
    return f(t), the actual level of bucket, which satisfy:
    1. d 2^f(t)/dt is constants
    2.  f(t0 + 2T) = 0
    3. f((t0) = L )
    */
    struct bucket *bucket = &ss->buckets[bucket_idx];
    if (ss->time_window_ms == 0) {
        return bucket->level;
    }

    unsigned L = bucket->level;
    long long T = ss->time_window_ms;
    long long t0 = bucket->last_update_ms;

    assert(t >= t0);
    if (t - t0 >= 2 * T) {
        return 0;
    }
    if (L <= 1) {
        return t - t0 >= T ? 0 : L;
    }

    long long tmp_exp = 1 << L;
    double a = ((double)(1 - tmp_exp)) / (2.0 * T);
    double b = (double)(tmp_exp);

    return (uint32_t)(log2(a * ((double)(t-t0)) + b) + 0.5);
}

static inline bool key_equal(const char *key1, size_t key1_len, const char *key2, size_t key2_len) {
    if (key1_len != key2_len) {
        return false;
    }
    return memcmp(key1, key2, key1_len) == 0;
}
static inline char *key_dup(const char *key, size_t key_len) {
    char *ret = malloc(key_len+1);
    memcpy(ret, key, key_len);
    ret[key_len] = '\0';
    return ret;
}

struct entry *smart_ptr_table_find(struct entry_table *table, const char *key, size_t key_len) {
    struct entry *ret;
    HASH_FIND(hh, table->entry, key, key_len, ret);
    if (ret == NULL || ret->dying) {
        return NULL;
    }
    return ret;
}

struct entry *smart_ptr_table_get(struct entry_table *table, const char *key, size_t key_len, void *arg) {
    struct entry *ret;
    HASH_FIND(hh, table->entry, key, key_len, ret);

    if (ret != NULL) {
        ret->dying = false;
        ret->ref_count++;
    } else {
        ret = malloc(sizeof(struct entry));
        ret->dying = false;
        ret->ref_count = 1;
        ret->key = key_dup(key, key_len);
        ret->key_len = key_len;
        if (arg == NULL) {
            ret->exdata = NULL;
        } else {
            ret->exdata = table->scheme.new_fn(arg);
        }
        HASH_ADD_KEYPTR(hh, table->entry, ret->key, ret->key_len, ret);
    }

    return ret;
}

int smart_ptr_table_release(struct entry_table *table, const char *key, size_t key_len) {
    struct entry *ret;
    HASH_FIND(hh, table->entry, key, key_len, ret);
    if (ret == NULL) {
        return -1;
    }

    ret->ref_count--;
    if (ret->ref_count == 0) {
        // printf("release %s\n", key);
        HASH_DEL(table->entry, ret);
        table->scheme.free_fn(ret->exdata);
        free(ret->key);
        free(ret);
    }

    return 0;
}

void smart_ptr_table_free(struct entry_table *table) {
    struct entry *current, *tmp;
    HASH_ITER(hh, table->entry, current, tmp) {
        HASH_DEL(table->entry, current);
        table->scheme.free_fn(current->exdata);
        free(current->key);
        free(current);
    }
    free(table);
}

struct entry_table *smart_ptr_table_new() {
    struct entry_table *table = malloc(sizeof(struct entry_table));
    table->entry = NULL;

    table->scheme = DEFAULT_SCHEME;

    return table;
}

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_ms, struct timeval now) {
    struct spread_sketch *pthis = calloc(1, sizeof(struct spread_sketch));

    pthis->depth = depth;
    pthis->width = width;
    pthis->time_window_ms = time_window_ms;
    pthis->precision = precision;

    pthis->reset_idx = 0;
    pthis->reset_time = now;

    pthis->buckets = calloc(depth * width, sizeof(struct bucket));
    pthis->scheme = DEFAULT_SCHEME;
    pthis->table = smart_ptr_table_new();
    pthis->table->scheme = pthis->scheme;

    for (int i = 0; i < depth * width; i++) {
        pthis->buckets[i].sthll_register = hll_create_register(precision);
    }

    pthis->level0_cnt = pthis->depth * pthis->width;

    return pthis;
}

void move_registers_forward(struct spread_sketch *ss, const struct timeval *now) {
    if (ss->time_window_ms == 0) {
        return;
    }

    long long reset_reg_count = hll_get_reset_register_count(ss->precision, ss->time_window_ms, *now, &ss->reset_time);
    if (reset_reg_count > 0) {
        for (int i = 0; i < ss->depth * ss->width; i++) {
            hll_reset_registers(ss->buckets[i].sthll_register, ss->precision, ss->reset_idx, reset_reg_count);
        }
        hll_advance_reset_index(&ss->reset_idx, reset_reg_count, ss->precision);
    }
}

// return 0 if not added, return 1 if added
int spread_sketch_add_hash(struct spread_sketch *ss, const char *entry, size_t entry_length, uint64_t item_hash, void *arg, struct timeval now) {
    uint32_t level = (uint32_t)__builtin_clzll(item_hash) + 1;
    long long now_ms = now.tv_sec * 1000 + now.tv_usec / 1000;

    if (item_hash == DUMMY_ITEM_HASH) {
        if (ss->level0_cnt == 0) {
            return 0;
        }
        assert(ss->level0_cnt>0);
    }
    
    // https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
    // A technique from the hashing literature is to use two hash functions h1(x) and h2(x) to simulate additional hash functions of the form gi(x) = h1(x) + ih2(x)
    // Assuming that the 128-bit xxhash function is perfect, we can view it as a combination of two 64-bit hash functions.
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(entry, entry_length, 171);
    uint32_t hash_x1 = (uint32_t) (hash_x_tmp >> 32);
    uint32_t hash_x2 = (uint32_t) hash_x_tmp;

    bool in_sketch = false;
    move_registers_forward(ss, &now);
    for (int i = 0; i < ss->depth; i++) {
        uint32_t hash_x = hash_x1 + i * hash_x2; 
        int bucket_idx = (hash_x % ss->width) + i * ss->width;
        struct bucket *bucket = &ss->buckets[bucket_idx];

        if (bucket->content != NULL && key_equal(bucket->content->key, bucket->content->key_len, entry, entry_length)) {
            bucket->content->dying = false;
            bucket->last_update_ms = now_ms;

            if (bucket->level < level) {
                if (bucket->level == 0) {
                    ss->level0_cnt--;
                }
                bucket->level = level;
            }
            in_sketch = true;
        } else {
            uint32_t true_level = bucket->content == NULL ? 0: cal_true_level(ss, bucket_idx, now_ms);

            if (level > true_level) {
                if (bucket->level == 0) {
                    ss->level0_cnt--;
                }
                
                // printf("update key %s to %s, and level %u to %u, in bucket (r,w)=(%d,%u)\n", bucket->content == NULL ? "NULL": bucket->content->key, key, true_level, level, i, hash_x % ss->width);
                const struct entry *content_old = bucket->content;
                if (content_old != NULL) {
                    smart_ptr_table_release(ss->table, content_old->key, content_old->key_len);
                }
                struct entry *content_new = smart_ptr_table_get(ss->table, entry, entry_length, arg);
                bucket->content = content_new;

                bucket->last_update_ms = now_ms;
                bucket->level = level;

                in_sketch = true;
            }
        }
        hll_add_hash(bucket->sthll_register, ss->precision, item_hash);
    }

    return in_sketch ? 1 : 0;
}

int spread_sketch_add(struct spread_sketch *ss, const char *entry, size_t entry_length, const char* item, size_t item_len, void *arg, struct timeval now) {
    uint64_t hash = XXH3_64bits(item, item_len);
    return spread_sketch_add_hash(ss, entry, entry_length, hash, arg, now);
}

double spread_sketch_point_query(const struct spread_sketch *ss, const char *entry, size_t entry_length) {
    uint64_t hash_x_tmp = XXH3_64bits_withSeed(entry, entry_length, 171);
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

struct spread_sketch *duplicate_and_step(const struct spread_sketch *ss, const struct timeval *now) {
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

void spread_sketch_free(struct spread_sketch *ss) {
    if (ss == NULL) {
        return;
    }
    smart_ptr_table_free(ss->table);
    for (int i = 0; i < ss->depth * ss->width; i++) {
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

        if (bucket_src->content == NULL || bucket_src->content->dying) {
            continue;
        }

        hll_merge(bucket_dst->sthll_register, src->buckets[i].sthll_register, dst->precision);
        if (bucket_dst->level == 0 && bucket_src->level > 0) {
            dst->level0_cnt--;
        }

        if (bucket_dst->content == NULL) {
            bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
            bucket_dst->level = bucket_src->level;
            bucket_dst->last_update_ms = bucket_src->last_update_ms;

            continue;
        }
        bucket_dst->content->dying = false;

        if (key_equal(bucket_src->content->key, bucket_src->content->key_len, bucket_dst->content->key, bucket_dst->content->key_len)) {
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
                smart_ptr_table_release(dst->table, bucket_dst->content->key, bucket_dst->content->key_len);
                bucket_dst->content = smart_ptr_table_get(dst->table, bucket_src->content->key, bucket_src->content->key_len, NULL);
                bucket_dst->last_update_ms = bucket_src->last_update_ms;
            }
        }
    }

    // for exdata
    const struct spread_sketch_scheme *scheme = &dst->table->scheme;

    struct entry *content_dest, *content_src, *tmp;
    HASH_ITER(hh, dst->table->entry, content_dest, tmp) {
        HASH_FIND(hh, src->table->entry, content_dest->key, content_dest->key_len, content_src);
        if (content_src == NULL || content_src->dying) {
            continue;
        }

        if (content_dest->exdata == NULL) {
            content_dest->exdata = scheme->copy_fn(content_src->exdata);
        } else {
            scheme->merge_fn(content_dest->exdata, content_src->exdata);
        }
    }
}

void *spread_sketch_get0_exdata(const struct spread_sketch *ss, const char *key, size_t key_len) {
    const struct entry *content = smart_ptr_table_find(ss->table, key, key_len);
    if (content == NULL) {
        return NULL;
    }
    return content->exdata;
}

void spread_sketch_reset(struct spread_sketch *ss) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        struct bucket *bucket = &ss->buckets[i];
        bucket->level = 0;
        hll_reset_registers(bucket->sthll_register, ss->precision, ss->reset_idx, 100000); // count is big enough
    }

    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        ss->scheme.reset_fn(content->exdata);
        content->dying = true;
    }

    ss->level0_cnt = ss->depth * ss->width;
}

void spread_sketch_set_exdata_schema(struct spread_sketch *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn) {
    ss->scheme.new_fn = new_fn;
    ss->scheme.free_fn = free_fn;
    ss->scheme.merge_fn = merge_fn;
    ss->scheme.reset_fn = reset_fn;
    ss->scheme.copy_fn = copy_fn;

    ss->table->scheme = ss->scheme;
}

int spread_sketch_get_count(const struct spread_sketch *ss) {
    int cnt = 0;
    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        if (!content->dying) {
            cnt++;
        }
    }

    return cnt;
}

size_t spread_sketch_list(const struct spread_sketch *ss, void **exdatas, size_t n_exdatas) {
    size_t count = 0;
    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        if (content->dying) {
            continue;
        }
        if (count >= n_exdatas) {
            break;
        }
        exdatas[count] = content->exdata;
        count++;
    }
    return count;
}

void spread_sketch_list_entries(const struct spread_sketch *ss, char ***entries, size_t **entry_lens, size_t *n_entry) {
    size_t count_max = HASH_COUNT(ss->table->entry);
    size_t count = 0;

    *entries = malloc(count_max * sizeof(char *));
    *entry_lens = malloc(count_max * sizeof(size_t));

    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        if (content->dying) {
            continue;
        }

        (*entries)[count] = content->key;
        (*entry_lens)[count] = content->key_len;
        count++;
    }

    *n_entry = count;
}

double spread_sketch_get_cardinality(const struct spread_sketch *ss, const char *key, size_t key_len) {
    if (smart_ptr_table_find(ss->table, key, key_len) == NULL) {
        return -1;
    }

    double est = spread_sketch_point_query(ss, key, key_len);
    return est;
}

struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src) {
    struct spread_sketch *dst = malloc(sizeof(struct spread_sketch));
    memcpy(dst, src, sizeof(struct spread_sketch));

    dst->buckets = calloc(dst->depth * dst->width, sizeof(struct bucket));
    dst->table = smart_ptr_table_new();
    spread_sketch_set_exdata_schema(dst, src->scheme.new_fn, src->scheme.free_fn, src->scheme.merge_fn, src->scheme.reset_fn, src->scheme.copy_fn);
    for (int i = 0; i < dst->depth * dst->width; i++) {
        dst->buckets[i].sthll_register = hll_duplicate(src->buckets[i].sthll_register, src->precision);
    }

    for (int i = 0; i < dst->depth * dst->width; i++) {
        if (src->buckets[i].content == NULL || src->buckets[i].content->dying) {
            continue;
        }
        dst->buckets[i].content = smart_ptr_table_get(dst->table, src->buckets[i].content->key, src->buckets[i].content->key_len, NULL);
        dst->buckets[i].level = src->buckets[i].level;
        if (dst->buckets[i].content->exdata == NULL) {
            dst->buckets[i].content->exdata = src->scheme.copy_fn(src->buckets[i].content->exdata);
        }
    }
    return dst;
}

void spread_sketch_recommend_parameters(int expected_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out)
{
    int logk = expected_super_spreader_number >= 3200 ? 4 : 3; // lg3200 = 3.51,round up to 4
    *depth_out = logk;

    int w;
    if (expected_super_spreader_number <= 100) {
        w = expected_super_spreader_number * 3 /2; // * 1.5, when the number is small, we need more width
    } else {
        w = expected_super_spreader_number + 50; // + 50: 100*1.5-100 = 50, make w=f(k) continuous
    }
    if (w < 40) {
        w = 40;
    }
    *width_out = w;

    *precision_out = 6;
}

void spread_sketch_get_parameter(const struct spread_sketch *ss, int *depth_out, int *width_out, unsigned char *precision_out, int *time_window_ms_out)
{
    *depth_out = ss->depth;
    *width_out = ss->width;
    *precision_out = ss->precision;
    *time_window_ms_out = ss->time_window_ms;
}

void spread_sketch_change_precision(struct spread_sketch *ss, unsigned char precision) {
    for (int i = 0; i < ss->depth * ss->width; i++) {
        hll_free_register(ss->buckets[i].sthll_register);
        ss->buckets[i].sthll_register = hll_create_register(precision);
    }
    ss->precision = precision;
}

void spread_sketch_serialize(const struct spread_sketch *ss, char **blob, size_t *blob_sz)
{
    /*
    format: 
    struct spread_sketch(including useless pointers)
    struct bucket * depth * width
    int64_t * depth * width for keylen + key（close-knit, key_len, key, key_len, key, ..., in order of bucket index）
    */
    // get serialize size 

    size_t sz = 0;
    sz += sizeof(struct spread_sketch);
    sz += ss->depth * ss->width * (sizeof(struct bucket) + hll_size(ss->precision) + sizeof(int64_t));
    for (int i = 0; i < ss->depth * ss->width; i++) {
        if (ss->buckets[i].content != NULL) {
            sz += ss->buckets[i].content->key_len;
        }
    }

    char *buffer = calloc(sz, sizeof(char));
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
    }
    for (int i = 0; i < ss->depth * ss->width; i++) {
        int64_t key_len;
        if (ss->buckets[i].content != NULL) {
            key_len = ss->buckets[i].content->key_len;
        } else {
            key_len = 0;
        }
        memcpy(buffer, &key_len, sizeof(int64_t));
        buffer += sizeof(int64_t);

        if (key_len > 0) {
            memcpy(buffer, ss->buckets[i].content->key, key_len);
            buffer += key_len;
        }
    }
}

struct spread_sketch *spread_sketch_deserialize(const char *blob, size_t blob_sz)
{
    struct spread_sketch *ss = malloc(sizeof(struct spread_sketch));
    memcpy(ss, blob, sizeof(struct spread_sketch));
    blob += sizeof(struct spread_sketch);
    ss->table = smart_ptr_table_new();
    ss->buckets = calloc(ss->depth * ss->width, sizeof(struct bucket));
    ss->scheme = DEFAULT_SCHEME;

    for (int i = 0; i < ss->depth * ss->width; i++) {
        struct bucket *bucket = &ss->buckets[i];
        memcpy(bucket, blob, sizeof(struct bucket));
        blob += sizeof(struct bucket);

        bucket->sthll_register = hll_create_register(ss->precision);
        memcpy(bucket->sthll_register, blob, hll_size(ss->precision));
        blob += hll_size(ss->precision);
    }

    for (int i = 0; i < ss->depth * ss->width; i++) {
        int64_t key_len;
        memcpy(&key_len, blob, sizeof(int64_t));
        blob += sizeof(int64_t);
        if (key_len == 0) {
            continue;
        }
        const char *key = blob;
        blob += key_len;

        struct entry *content = smart_ptr_table_get(ss->table, key, key_len, NULL);
        ss->buckets[i].content = content;
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

    size_t bucket_size = sizeof(struct bucket) + hll_size(ss->precision);
    ret += ss->depth * ss->width * bucket_size;

    struct entry *content, *tmp;
    HASH_ITER(hh, ss->table->entry, content, tmp) {
        ret += sizeof(struct entry);
        ret += content->key_len;
        // assume the exdata is NULL
    }
    return ret;
}

double spread_sketch_query(const struct spread_sketch *ss, const char *key, size_t key_length, struct timeval now) {    
    if (ss->time_window_ms!=0 && hll_should_slide(ss->precision, ss->time_window_ms, now, ss->reset_time)) {
        struct spread_sketch *duplicate = duplicate_and_step(ss, &now);
        double ret = spread_sketch_point_query(duplicate, key, key_length);
        spread_sketch_free(duplicate);
        return ret;
    } else {
        return spread_sketch_point_query(ss, key, key_length);
    }
}

struct spread_sketch *spread_sketch_replicate(uuid_t uuid, const char *blob, size_t blob_sz) {
    return spread_sketch_deserialize(blob, blob_sz);
}
