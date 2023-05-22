#include "heavy_keeper.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include "heap.h"
#include "mpack.h"
#include "uthash.h"
#include "sorted_set.h"
#include "xxhash.h"

#define FP_HASH_KEY 0


struct Bucket {
    unsigned int finger_print;
    unsigned count; // the actual count, sum of all uuid_hash_node->count
};

struct replica_node {
    uuid_t uuid;
    struct Bucket *sketch;
    unsigned version;
    UT_hash_handle hh;
};

struct replica_manager { // hash replica
    struct replica_node *replica_tbl;
    struct replica_node *my_replica;
    size_t sketch_len; // equals to params->array_num * params->max_bucket_num
};

struct heavy_keeper {
    int K;
    struct sorted_set *top_K_heap;
    struct heavy_keeper_options params;
    struct replica_manager *replica_manager;
};

struct Bucket *new_sketch(struct heavy_keeper_options *params) {
    size_t array_len = (size_t)params->array_num * (size_t)params->max_bucket_num;
    size_t alloc_size = array_len * sizeof(struct Bucket);
    struct Bucket *ret = (struct Bucket *)malloc(alloc_size);
    memset(ret, 0, alloc_size);

    return ret;
}

void params_set_to_default(struct heavy_keeper_options *p, int K) {
    double log_ret = log((double)K) / 2.3;  // 2.3: log_e(10), log_ret = log_10(K)

    if (log_ret < 3) {
        p->array_num = 3;
    } else {
        p->array_num = (int)(log_ret);
    }
    p->decay_exp_rate = 1.17; // by test, 1.17 is the best
    p->max_bucket_num = (int)(log_ret * K * 2);
    if (p->max_bucket_num < 150) {
        p->max_bucket_num = 150;  // determined through test, too small max_bucket_num will let accuracy decrease severely.
    } else if (p->max_bucket_num > 600) {
        p->max_bucket_num = p->max_bucket_num / 4 + 450;
    }
}

void params_set_to(struct heavy_keeper_options *p, struct heavy_keeper_options *src) {
    if (src->array_num != 0) {
        p->array_num = src->array_num;
    }
    if (src->decay_exp_rate != 0) {
        p->decay_exp_rate = src->decay_exp_rate;
    }
    if (src->max_bucket_num != 0) {
        p->max_bucket_num = src->max_bucket_num;
    }
}

struct replica_manager *peer_manager_new_with_uuid(struct heavy_keeper_options *p, uuid_t uuid)
{
    struct replica_manager *pm = (struct replica_manager *)malloc(sizeof(struct replica_manager));
    memset(pm, 0, sizeof(struct replica_manager));
    struct replica_node *node = (struct replica_node *)malloc(sizeof(struct replica_node));
    node->version = 0;
    node->sketch = new_sketch(p);
    uuid_copy(node->uuid, uuid);
    HASH_ADD(hh, pm->replica_tbl, uuid, sizeof(uuid_t), node);
    pm->my_replica = node;

    pm->sketch_len = (size_t)p->array_num * (size_t)p->max_bucket_num;
    return pm;
}

struct replica_manager *peer_manager_new(struct heavy_keeper_options *p)
{
    uuid_t uuid;
    uuid_generate(uuid);
    return peer_manager_new_with_uuid(p, uuid);
}

void peer_manager_free(struct replica_manager *pm)
{
    struct replica_node *node = NULL;
    struct replica_node *tmp = NULL;
    HASH_ITER(hh, pm->replica_tbl, node, tmp) {
        HASH_DEL(pm->replica_tbl, node);
        free(node->sketch);
        free(node);
    }
    free(pm);
}

struct heavy_keeper *heavy_keeper_new_with_uuid(int max_query_num, struct heavy_keeper_options *params, uuid_t uuid) {
    struct heavy_keeper *hk = (struct heavy_keeper *)malloc(sizeof(struct heavy_keeper));
    hk->K = max_query_num;

    hk->top_K_heap = sorted_set_new(max_query_num);

    params_set_to_default(&(hk->params), max_query_num);

    if (params != NULL) {
        params_set_to(&(hk->params), params);
    }
    hk->replica_manager = peer_manager_new_with_uuid(&(hk->params), uuid);
    return hk;
}

struct heavy_keeper *heavy_keeper_new(int max_query_num, struct heavy_keeper_options *params) {
    uuid_t uuid;
    uuid_generate(uuid);

    return heavy_keeper_new_with_uuid(max_query_num, params, uuid);
}

struct heavy_keeper *heavy_keeper_new_for_swarmkv_cb(uuid_t uuid)
{
    return heavy_keeper_new_with_uuid(HK_DEFAULT_K, NULL, uuid);
}

struct heavy_keeper_result *heavy_keeper_result_new(struct heavy_keeper *hk) {
    size_t expected_max_len = hk->K;
    struct heavy_keeper_result *stats_hd = (struct heavy_keeper_result *)malloc(sizeof(struct heavy_keeper_result));
    memset(stats_hd, 0, sizeof(struct heavy_keeper_result));
    stats_hd->count = (unsigned *)malloc(expected_max_len * sizeof(unsigned));
    stats_hd->key = (char **)malloc(expected_max_len * sizeof(char *));
    stats_hd->key_len = (size_t *)malloc(expected_max_len * sizeof(size_t));
    return stats_hd;
}

void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd) {
    for (int i = 0; i < stats_hd->n_key; i++) {
        free(stats_hd->key[i]);
    }
    free(stats_hd->count);
    free(stats_hd->key);
    free(stats_hd->key_len);
    free(stats_hd);
}

void heavy_keeper_free(struct heavy_keeper *hk) {
    sorted_set_free(hk->top_K_heap);
    peer_manager_free(hk->replica_manager);
    free(hk);
}

void heavy_keeper_get_params(struct heavy_keeper *hk,  struct heavy_keeper_options *params, int *K) {
    *params = hk->params;
    *K = hk->K;
}

/*********************************add and query*************************************/
/***********************************************************************************/
bool if_need_to_decay(struct heavy_keeper *hk, struct Bucket *bucket, unsigned count) {
    double exp = (double)bucket->count / count;
    if (exp > 250) {  // decay_exp_rate ^ (-250) is very small, so regard the chance as 0.
        return false;
    }

    double r = (double)rand() / (double)RAND_MAX;

    double b = hk->params.decay_exp_rate;
    double decay_rate = pow(b, -exp);

    if (r < decay_rate) {
        return true;
    }
    return false;
}

static uint64_t __fnv_1a(const char *key, size_t len, int seed) {
    // FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
    size_t i;
    uint32_t h = 2166136261UL + (seed * 15);  // FNV_OFFSET 32 bit with magic number seed
    for (i = 0; i < len; ++i) {
        h = h ^ (unsigned char)key[i];
        h = h * 16777619UL;  // FNV_PRIME 32 bit
    }
    return h;
}

unsigned hash_xxhash_flow_id(const char *key, size_t key_len, unsigned seed) {
    // XXH64_hash_t res_64 = XXH3_64bits_withSeed(key, key_len + 1, seed);
    // return res_64 >> 32;
    // unsigned long int res = XXH32(key, key_len + 1, seed);
    // return res;
    return __fnv_1a(key, key_len, seed);
}

unsigned count_add(unsigned a, unsigned b) {
    if (a > UINT32_MAX - b) {
        return UINT32_MAX;
    }
    return a + b;
}

unsigned my_max(unsigned a, unsigned b) {
    if (a > b) {
        return a;
    }
    return b;
}


int tmp_cmp_bucket(const void* a, const void* b)
{
    struct Bucket *pa = (struct Bucket *)a;
    struct Bucket *pb = (struct Bucket *)b;
    if (pa->finger_print > pb->finger_print) {
        return -1;
    } else if (pa->finger_print < pb->finger_print) {
        return 1;
    } else {
        return 0;
    }
}
void get_merged_replica_in_pos(struct heavy_keeper *hk, size_t index, struct Bucket *b_out)
{
    struct replica_node *node = NULL, *tmp = NULL;
    struct Bucket *tmp_B = NULL;

    size_t replica_num = HASH_COUNT(hk->replica_manager->replica_tbl);
    struct Bucket fp_arr[replica_num];
    memset(fp_arr, 0, sizeof(struct Bucket) * replica_num);
    int fp_num = 0;

    fp_num = 0;
    HASH_ITER(hh, hk->replica_manager->replica_tbl, node, tmp) {
        tmp_B = &node->sketch[index];
        fp_arr[fp_num].finger_print = tmp_B->finger_print;
        fp_arr[fp_num].count = tmp_B->count;
        fp_num++;
    }
    qsort(fp_arr, fp_num, sizeof(struct Bucket), tmp_cmp_bucket);

    unsigned int tmp_count_of_fp = 0;
    memset(b_out, 0, sizeof(struct Bucket));
    for (int i = 0; i < fp_num; i++) {
        if (i == 0 || fp_arr[i].finger_print != fp_arr[i - 1].finger_print) {
            tmp_count_of_fp = fp_arr[i].count;
        } else {
            tmp_count_of_fp = count_add(tmp_count_of_fp, fp_arr[i].count);
        }
        if (tmp_count_of_fp > b_out->count) {
            b_out->count = tmp_count_of_fp;
            b_out->finger_print = fp_arr[i].finger_print;
        }
    }
}

void heavy_keeper_add(struct heavy_keeper *heavy_keeper, const char *key, size_t key_len, unsigned int count) {
    if (key_len == 0) {
        printf("key_len should not be 0\n");
        return;
    }

    struct sorted_set *summary = heavy_keeper->top_K_heap;
    unsigned int FP = hash_xxhash_flow_id(key, key_len, FP_HASH_KEY);
    unsigned nMin = sorted_set_get_min_score(summary);
    unsigned maxv = 0;
    unsigned Hsh = 0;
    int w = heavy_keeper->params.max_bucket_num;
    bool my_sketch_changed = false;

    for (int j = 0; j < heavy_keeper->params.array_num; j++) {
        if (j == FP_HASH_KEY) {
            Hsh = FP % w;
        } else {
            Hsh = hash_xxhash_flow_id(key, key_len, j) % w;
        }
        int tmp_index = j * w + Hsh;
        struct Bucket bucket;
        get_merged_replica_in_pos(heavy_keeper, tmp_index, &bucket);
        struct Bucket *my_bucket = &(heavy_keeper->replica_manager->my_replica->sketch[tmp_index]); // sketch of the current process.
        unsigned int my_count_before = my_bucket->count;

        if (bucket.finger_print == FP) {
            // If a flow is not in the min-heap, then the estimated flow size should be no larger than nmin.
            // The flows whose counts are both larger than nmin and not in min-heap must have the same xxhash value, where its FP stored in bucket represents another flowid,
            // In this case, the sketch won't be updated. This flow is expected to be taken into account in another array,
            // where its FP is different from the one it should collided with so element flows won't be missed.
            if (bucket.count > nMin && sorted_set_get_member_score(summary, key, key_len) == 0) {
                    continue;
            }
            my_bucket->count = count_add(my_bucket->count, count);
            maxv = my_max(maxv, count_add(bucket.count, count));
        } else {
            if (!if_need_to_decay(heavy_keeper, &bucket, count)) {
                continue;
            }
            if (my_bucket->count < count) {
                my_bucket->finger_print = FP;
                my_bucket->count = count;

                maxv = my_max(maxv, count);
            } else {
                my_bucket->count = my_bucket->count - count;
                heavy_keeper->replica_manager->my_replica->version++;
            }
            if (my_count_before != my_bucket->count) {
                my_sketch_changed = true;
            }
        }
    }
    if (my_sketch_changed) {
        heavy_keeper->replica_manager->my_replica->version++;
    }

    unsigned int tmp_cnt = sorted_set_get_member_score(summary, key, key_len);
    if (tmp_cnt == 0) {  // the flow is not in sorted set
        if ((maxv - nMin <= count && maxv != nMin) || sorted_set_cardinality(summary) != heavy_keeper->K) {
            sorted_set_insert(summary, key, key_len, maxv);
        }
    } else {
        if (maxv > tmp_cnt) {
            sorted_set_incrby(summary, key, key_len, maxv - tmp_cnt);
        }
    }
}

struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk) {
    struct heavy_keeper_result *stats  = heavy_keeper_result_new(hk);
    int heap_entry_num = sorted_set_cardinality(hk->top_K_heap);

    unsigned *keys = (unsigned *)malloc(heap_entry_num * sizeof(unsigned));
    char **vals = (char **)malloc(heap_entry_num * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(heap_entry_num * sizeof(size_t));
    sorted_set_dump(hk->top_K_heap, keys, vals, val_lens);

    int query_entry_num = heap_entry_num > hk->K ? hk->K : heap_entry_num;
    stats->n_key = query_entry_num;
    int index_q = 0;
    for (int i = query_entry_num - 1; i >= 0; i--) {
        stats->key[index_q] = (char *)malloc(val_lens[i]);
        strcpy(stats->key[index_q], vals[i]);
        stats->count[index_q] = keys[i];
        stats->key_len[index_q] = val_lens[i] - 1;
        index_q++;
    }

    free(keys);
    free(vals);
    free(val_lens);
    return stats;
}

/*****************************************merge *****************************************/
/****************************************************************************************/
static void heavy_keeper_fetch_sketch_from_replicas(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    struct replica_node *node_src = NULL, *tmp = NULL, *node_dest = NULL;
    struct replica_manager *pm_dest = dest->replica_manager;
    struct replica_manager *pm_src = src->replica_manager;
    size_t sketch_size = sizeof(struct Bucket) * dest->replica_manager->sketch_len;

    HASH_ITER(hh, pm_src->replica_tbl, node_src, tmp) {
        HASH_FIND(hh, pm_dest->replica_tbl, node_src->uuid, sizeof(uuid_t), node_dest);
        if (node_dest == NULL) {
            // copy sketch from src to dest, as well as the version
            node_dest = (struct replica_node *)malloc(sizeof(struct replica_node));
            uuid_copy(node_dest->uuid, node_src->uuid);

            node_dest->sketch = (struct Bucket *)malloc(sketch_size);
            memcpy(node_dest->sketch, node_src->sketch, sketch_size);
            node_dest->version = node_src->version;
            HASH_ADD(hh, pm_dest->replica_tbl, uuid, sizeof(uuid_t), node_dest);
        } else {
            if (node_dest->version >= node_src->version) {
                continue;
            }
            node_dest->version = node_src->version;
            memcpy(node_dest->sketch, node_src->sketch, sketch_size);
        }
    }
}

/* 
This function is deprecated. Because sketch_observed is not stored in heavykeeper, which we calculate in heavy_keeper_add instead.
*/
void heavy_keeper_merge_all_sketch_in_replicas_deprecated(struct heavy_keeper *hk, struct Bucket *sketch_out) {
    int w = hk->params.max_bucket_num;
    int d = hk->params.array_num;
    memset(sketch_out, 0, sizeof(struct Bucket) * w * d);

    struct replica_node *node = NULL, *tmp = NULL;
    struct Bucket *sketch = NULL;
    struct Bucket *tmp_B = NULL;
    size_t tmp_idx = 0;

    size_t replica_num = 0;
    HASH_ITER(hh, hk->replica_manager->replica_tbl, node, tmp) {
        replica_num++;
    }
    struct Bucket fp_arr[replica_num];
    memset(fp_arr, 0, sizeof(struct Bucket) * replica_num);
    int fp_num = 0;
    unsigned int tmp_count_of_fp = 0;

    for (int array_idx = 0; array_idx < d; array_idx++) {
        for (int bucket_idx = 0; bucket_idx < w; bucket_idx++) {
            fp_num = 0;
            tmp_idx = array_idx * w + bucket_idx;
            HASH_ITER(hh, hk->replica_manager->replica_tbl, node, tmp) {
                sketch = node->sketch;
                tmp_B = &sketch[tmp_idx];
                fp_arr[fp_num].finger_print = tmp_B->finger_print;
                fp_arr[fp_num].count = tmp_B->count;
                fp_num++;
            }
            qsort(fp_arr, fp_num, sizeof(struct Bucket), tmp_cmp_bucket);

            tmp_count_of_fp = 0;
            for (int i = 0; i < fp_num; i++) {
                if (i == 0 || fp_arr[i].finger_print != fp_arr[i - 1].finger_print) {
                    tmp_count_of_fp = fp_arr[i].count;
                } else {
                    tmp_count_of_fp = count_add(tmp_count_of_fp, fp_arr[i].count);
                }
                if (tmp_count_of_fp > sketch_out[tmp_idx].count) {
                    sketch_out[tmp_idx].count = tmp_count_of_fp;
                    sketch_out[tmp_idx].finger_print = fp_arr[i].finger_print;
                }
            }
        }
    }
}

unsigned find_maxv_in_sketch(const struct Bucket *sketch, struct heavy_keeper_options *param, const char *flow_id, int flow_id_len) {
    const struct Bucket *bucket;
    int width = param->max_bucket_num;
    int array_num = param->array_num;
    unsigned fp = hash_xxhash_flow_id(flow_id, flow_id_len, FP_HASH_KEY);

    unsigned maxv = 0;
    unsigned int Hsh;
    for (int array_idx = 0; array_idx < array_num; array_idx++) {
        Hsh = hash_xxhash_flow_id(flow_id, flow_id_len, array_idx) % width;
        bucket = &(sketch[array_idx * width + Hsh]);

        if (bucket->finger_print == fp) {
            maxv = my_max(maxv, bucket->count);
        }
    }

    return maxv;
}

unsigned int find_maxv_for_array(unsigned int hash_each_array[], int array_num, int width, int fp, struct replica_node *node)
{
     const struct Bucket *bucket;
    unsigned maxv = 0;
        for (int array_idx = 0; array_idx < array_num; array_idx++) {
            unsigned hash = hash_each_array[array_idx];
            bucket = &(node->sketch[array_idx * width + hash]);

            if (bucket->finger_print == fp) {
                maxv = my_max(maxv, bucket->count);
            }
        }
        return maxv;
}

unsigned int estimate_the_count_by_replicas(struct heavy_keeper *hk, const char *flow_id_str, int flow_id_len)
{
    struct replica_node *node = NULL, *tmp = NULL;
    int width = hk->params.max_bucket_num;
    int array_num = hk->params.array_num;
    unsigned fp = hash_xxhash_flow_id(flow_id_str, flow_id_len, FP_HASH_KEY);

    // hash algorithm is inaffected by what replica it is, so calculate it first.
    unsigned int hash_each_array[array_num];
    for (int array_idx = 0; array_idx < array_num; array_idx++) {
        hash_each_array[array_idx] = hash_xxhash_flow_id(flow_id_str, flow_id_len, array_idx) % width;
    }

    unsigned count_estimate = 0;
    unsigned maxv;
    HASH_ITER(hh, hk->replica_manager->replica_tbl, node, tmp) {
        maxv = find_maxv_for_array(hash_each_array, array_num, width, fp, node);
        count_estimate += maxv;
    }
    return count_estimate;
}

void heavy_keeper_merge_minheap(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    struct sorted_set *new_rec = sorted_set_new(dest->K);
    int size_dest = sorted_set_cardinality(dest->top_K_heap);
    int size_src = sorted_set_cardinality(src->top_K_heap);
    int max_size = size_dest > size_src ? size_dest : size_src;
    unsigned *keys = (unsigned *)malloc(max_size * sizeof(unsigned));
    char **vals = (char **)malloc(max_size * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(max_size * sizeof(size_t));

    // all flow id in src
    sorted_set_dump(src->top_K_heap, keys, vals, val_lens);
    char *flow_id_str = NULL;
    size_t flow_id_len = 0;
    for (int i = 0; i < size_src; i++) {
        flow_id_str = vals[i];
        flow_id_len = val_lens[i] - 1;

        unsigned int count_estimate = estimate_the_count_by_replicas(dest, flow_id_str, flow_id_len);
        sorted_set_insert(new_rec, flow_id_str, flow_id_len, count_estimate);
    }

    // all flow id in dest
    sorted_set_dump(dest->top_K_heap, keys, vals, val_lens);
    for (int i = 0; i < size_dest; i++) {
        flow_id_str = vals[i];
        flow_id_len = val_lens[i] - 1;

        if (sorted_set_get_member_score(new_rec, flow_id_str, flow_id_len) != 0) {
            continue;
        }

        unsigned int count_estimate = estimate_the_count_by_replicas(dest, flow_id_str, flow_id_len);
        sorted_set_insert(new_rec, flow_id_str, flow_id_len, count_estimate);
    }

    free(keys);
    free(vals);
    free(val_lens);
    sorted_set_free(dest->top_K_heap);

    dest->top_K_heap = new_rec;
}

int heavy_keeper_merge(struct heavy_keeper *dest, const struct heavy_keeper *src) {
    if (dest->params.array_num != src->params.array_num ||
        dest->params.max_bucket_num != src->params.max_bucket_num ||
        dest->K != src->K) {
        printf("ERROR: parameters of two merged heavy keeper must be the same\n");
        return -1;
    }

    heavy_keeper_fetch_sketch_from_replicas(dest, src);

    heavy_keeper_merge_minheap(dest, src);
    return 0;
}

/******************************* serialization *******************************/
/*Serialize the heavy keeper to a mpack string which is equivalent to a JSON
    The json: 
    {
    "K": <value_of_stats_num>,
    "width": <value_of_param.max_bucket_num>,
    "depth": <value_of_param.array_num>,
    "b": <value_of_param.decay_exp_rate>,
    “my_uuid": <value of peer_manager.my_uuid>
    "rm": [
        {"uuid": <uuid_in_peer_manager.uuid_matcher>
        "sketch": <a_binary_array_of_struct_buckets>
        "ver": <the version of uuid>
        }
        {(more elements in rm, the replica manager)}
        ...
    ]
    "ss": [
        {
            "member": <flow_id_string>,
        },
        {(more elements in ss, the sorted set)}
        ...
        }
    ]
}
***********************************************************************************/
void heavy_keeper_bucket_serialization(struct replica_node *node, size_t sketch_len, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "uuid");
    mpack_write_str(&writer, (char *)node->uuid, sizeof(uuid_t));
    mpack_write_cstr(&writer, "sketch");
    mpack_write_bin(&writer, (const char *)node->sketch, sizeof(struct Bucket) * sketch_len);

    mpack_write_cstr(&writer, "ver");
    mpack_write_u64(&writer, node->version);
    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_peer_manager_serialization!\n");
    }
    return;
}

int heavy_keeper_sorted_set_member_serialization(unsigned int key, const char *val, size_t val_len, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);

    mpack_build_map(&writer);
    mpack_write_cstr(&writer, "member");
    mpack_write_str(&writer, val, val_len - 1);
    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_sorted_set_member_serialization!\n");
        return -1;
    }
    return 0;
}

void heavy_keeper_param_serialization(const struct heavy_keeper *hk, mpack_writer_t *writer)
{
    mpack_write_cstr(writer, "width");
    mpack_write_int(writer, hk->params.max_bucket_num);
    mpack_write_cstr(writer, "depth");
    mpack_write_int(writer, hk->params.array_num);
    mpack_write_cstr(writer, "b");
    mpack_write_double(writer, hk->params.decay_exp_rate);
}

void heavy_keeper_sketch_replica_serialization(const struct heavy_keeper *hk, mpack_writer_t *writer)
{
    size_t element_mpack_size = 0;
    char *element_mpack_buff = NULL;
    mpack_write_cstr(writer, "rm");
    mpack_build_array(writer);
    size_t bucket_len = hk->replica_manager->sketch_len;
    struct replica_node *node, *tmp_node;
    HASH_ITER(hh, hk->replica_manager->replica_tbl, node, tmp_node) {
        heavy_keeper_bucket_serialization(node, bucket_len, &element_mpack_buff, &element_mpack_size);
        mpack_write_object_bytes(writer, element_mpack_buff, element_mpack_size);
        free(element_mpack_buff);
        element_mpack_size = 0;
    }
    mpack_complete_array(writer);
}

void heavy_keeper_sorted_set_serialization(const struct heavy_keeper *hk, mpack_writer_t *writer)
{
    size_t element_mpack_size = 0;
    char *element_mpack_buff = NULL;
    mpack_write_cstr(writer, "ss");
    int heap_entry_num = sorted_set_cardinality(hk->top_K_heap);
    unsigned *keys = (unsigned *)malloc(heap_entry_num * sizeof(unsigned));
    char **vals = (char **)malloc(heap_entry_num * sizeof(char *));
    size_t *val_lens = (size_t *)malloc(heap_entry_num * sizeof(size_t));
    sorted_set_dump(hk->top_K_heap, keys, vals, val_lens);
    mpack_build_array(writer);
    for (size_t i = 0; i < heap_entry_num; i++) {
        heavy_keeper_sorted_set_member_serialization(keys[i], vals[i], val_lens[i], &element_mpack_buff, &element_mpack_size);
        mpack_write_object_bytes(writer, element_mpack_buff, element_mpack_size);
        free(element_mpack_buff);
        element_mpack_size = 0;
    }
    mpack_complete_array(writer);
    free(keys);
    free(vals);
    free(val_lens);
}

void heavy_keeper_serialization(const struct heavy_keeper *hk, char **blob, size_t *blob_sz) {
    mpack_writer_t writer;
    mpack_writer_init_growable(&writer, blob, blob_sz);
    mpack_build_map(&writer);

    mpack_write_cstr(&writer, "K");
    mpack_write_int(&writer, hk->K);
    heavy_keeper_param_serialization(hk, &writer);
    mpack_write_cstr(&writer, "my_uuid");
    mpack_write_str(&writer, (char *)hk->replica_manager->my_replica->uuid, sizeof(uuid_t));
    heavy_keeper_sketch_replica_serialization(hk, &writer);
    heavy_keeper_sorted_set_serialization(hk, &writer);

    mpack_complete_map(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        fprintf(stderr, "An error occurred encoding the data!\n");
    }
}

void heavy_keeper_peer_deserialization(mpack_node_t root, struct replica_node *out)
{
    mpack_node_t item = mpack_node_map_cstr(root, "uuid");
    memcpy(out->uuid, mpack_node_str(item), sizeof(uuid_t));
    mpack_node_t item_sketch = mpack_node_map_cstr(root, "sketch");
    size_t bin_size = mpack_node_bin_size(item_sketch);

    out->sketch = (struct Bucket *)malloc(bin_size);
    memcpy(out->sketch, mpack_node_bin_data(item_sketch), bin_size);

    out->version = mpack_node_u64(mpack_node_map_cstr(root, "ver"));
}

void heavy_keeper_sorted_set_deserialization(mpack_node_t *root, struct sorted_set *out)
{
    mpack_node_t item;
    mpack_node_t array_sorted_set = mpack_node_map_cstr(*root, "ss");
    size_t sorted_set_size = mpack_node_array_length(array_sorted_set);
    for (size_t i = 0; i < sorted_set_size; i++) {
        item = mpack_node_array_at(array_sorted_set, i);
        mpack_node_t flow_id_str = mpack_node_map_cstr(item, "member");
        const char *val = mpack_node_str(flow_id_str);
        size_t val_len = mpack_node_strlen(flow_id_str);
        sorted_set_insert(out, val, val_len, 0);
    }
}

void heavy_keeper_param_deserialization(mpack_node_t *root, struct heavy_keeper_options *out)
{
    int width = mpack_node_int(mpack_node_map_cstr(*root, "width"));
    int depth = mpack_node_int(mpack_node_map_cstr(*root, "depth"));
    double decay_rate = mpack_node_double(mpack_node_map_cstr(*root, "b"));

    out->max_bucket_num = width;
    out->array_num = depth;
    out->decay_exp_rate = decay_rate;
}

void heavy_keeper_replica_manager_deserialization(mpack_node_t *root, struct heavy_keeper *hk)
{
    mpack_node_t item;
    mpack_node_t array_pm = mpack_node_map_cstr(*root, "rm");
    int pm_len = mpack_node_array_length(array_pm);
    struct replica_node *hash_tbl = hk->replica_manager->replica_tbl;
    struct replica_node *tmp_node = NULL;
    for (int i = 0; i < pm_len; i++) {
        item = mpack_node_array_at(array_pm, i);
        tmp_node = (struct replica_node *)malloc(sizeof(struct replica_node));
        heavy_keeper_peer_deserialization(item, tmp_node);
        HASH_ADD(hh, hash_tbl, uuid, sizeof(uuid_t), tmp_node);
    }
    hk->replica_manager->replica_tbl = hash_tbl;
}

struct heavy_keeper *heavy_keeper_deserialization(const char *blob, size_t size) {
    mpack_tree_t tree;
    mpack_tree_init_data(&tree, blob, size);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

    int K = mpack_node_int(mpack_node_map_cstr(root, "K"));
    struct heavy_keeper_options params;
    heavy_keeper_param_deserialization(&root, &params);
    uuid_t my_uuid;
    memcpy(my_uuid, mpack_node_str(mpack_node_map_cstr(root, "my_uuid")), sizeof(uuid_t));

    struct heavy_keeper *hk = heavy_keeper_new_with_uuid(K, &params, my_uuid);

    heavy_keeper_replica_manager_deserialization(&root, hk);
    heavy_keeper_sorted_set_deserialization(&root, hk->top_K_heap);

    if (mpack_tree_destroy(&tree) != mpack_ok) {
        fprintf(stderr, "An error occurred in heavy_keeper_deserialization!\n");
        return NULL;
    }
    return hk;
}

void heavy_keeper_merge_blob(struct heavy_keeper *hk, const char *blob, size_t blob_sz)
{
	struct heavy_keeper *to_merge=heavy_keeper_deserialization(blob, blob_sz);
	heavy_keeper_merge(hk, to_merge);
	heavy_keeper_free(to_merge);
	return;
}

size_t cal_one_replica_memory_usage(struct replica_manager *r)
{
    size_t total_memory_usage = 0;
    total_memory_usage += sizeof(struct replica_node);
    total_memory_usage += sizeof(uuid_t);
    total_memory_usage += sizeof(struct Bucket) * r->sketch_len;

    return total_memory_usage;
}

size_t heavy_keeper_cal_total_memory_usage(const struct heavy_keeper *hk)
{
    size_t total_memory_usage = 0;
    total_memory_usage += sizeof(struct heavy_keeper);
    total_memory_usage += sorted_set_cal_total_memory_usage(hk->top_K_heap);

    total_memory_usage += cal_one_replica_memory_usage(hk->replica_manager) * HASH_COUNT(hk->replica_manager->replica_tbl);
    return total_memory_usage;
}
