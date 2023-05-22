#pragma once

#ifdef __cplusplus
extern "C"{
#endif

#include <stddef.h>
#include <uuid/uuid.h>

/* ---------------------------------------------------------------------------------------
TOP K using heavykeeper algorithm. 
Author: Chen Zizhan 
Time: 2023/4/26

# usage example
struct heavy_keeper *hk = heavy_keeper_new(K, NULL);

// or: run heavykeeper with user defined params: 
// heavy_keeper_options params = {5, 1000, 1.5};
// struct heavy_keeper *hk = heavy_keeper_new(K, &params);

const char flow_id[] = "any string representing url, ip, etc";
unsigned count = 1; // if count the number of session/packet/... 
// Or: if sum up the total traffic volume in bytes
// unsigned count = 700;

heavy_keeper_add(hk, flow_id, strlen(flow_id), count);
// repeat update for every flow ... ... ... ... 
// ...........................
struct heavy_keeper_result* stats = heavy_keeper_result_new(K);
heavy_keeper_query(hk, stats);

heavy_keeper_free(hk);

use the stats variable
For example :
  for (int i = 0; i < stats->n_key; i++) {
      printf("top %d flow id is: %s, its count: #llu", i, stats->key[i], stats->count[i]);
  }

heavy_keeper_result_free(stats);

---------------------------------------------------------------------------------------------*/
#define HK_DEFAULT_K 100 // the size of heavy keeper created by swarm kv cmd callbacks

// The handler of heavykeeper
struct heavy_keeper;

// Query result
struct heavy_keeper_result {
    size_t n_key; // number of result
    char **key; // key[i] is the flow id, i < n_key
    size_t *key_len; // key_len[i] is the length flow id string, i < n_key
    unsigned *count; // count[i] is the count of the corresponding flow id
};

// Parameters used in algorithm
struct heavy_keeper_options{
    int array_num;  // the size of the array. Default value: 4
                    // Set it by how accurate you want. Value of 4 garentees an accuray more than 90% and around 97% in all tests.
                    // Not too big because it have an impact on both time and memory efficiency.
    int max_bucket_num; // M2, the maximum number of buckets every array keeps. Default value: k*log(k) and no less than 100. 
                        // Basically, as long as big enough, it won't affect the accuracy significantly.
    double decay_exp_rate; // b, bigger variance of flow size is(elephant flows take more ratio), smaller it should be.
                           //  Must bigger than 1. Better not bigger than 1.3, otherwise some elephant flow will be missed.
};

/**
 * @brief   Create a heavy keeper.
 * @param   max_query_num            you want to get a summery of top-max_query_num flows.
 * @param   params                   parameters used in algorithm. If NULL, default params will be used.
 * @returns a pointer to the heavy keeper.
 */
struct heavy_keeper *heavy_keeper_new(int max_query_num, struct heavy_keeper_options *params);

/* 
    equal to heavy_keeper_new(HK_DEFAULT_K, NULL) with a uuid prescribed by swarmkv.
*/
struct heavy_keeper *heavy_keeper_new_for_swarmkv_cb(uuid_t uuid);

/**
 * @brief   free a heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 */
void heavy_keeper_free(struct heavy_keeper *hk);

/**
 * @brief   Add a flow to the heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   key           the flow id string.
 * @param   key_len       the length of the flow id string.
 * @param   count         the count of the flow or its traffic volume in bytes.
 */
void heavy_keeper_add(struct heavy_keeper *hk, const char *key, size_t key_len, unsigned int count);

/**
 * @brief   Get the parameters used in algorithm.
 * @param   hk            the pointer to the heavy keeper.
 * @param   params        output of the parameters.
 * @param   K             output of the max_query_num.
 */
void heavy_keeper_get_params(struct heavy_keeper *hk,  struct heavy_keeper_options *params, int *K);

/**
 * @brief   Query the top-K flows.The flow id with bigger count ranks at smaller index.
 * @param   hk            the pointer to the heavy keeper.
 * @return  a pointer to the heavy keeper query result. User should free it after use by calling heavy_keeper_result_free.
 */
struct heavy_keeper_result *heavy_keeper_query(struct heavy_keeper *hk);

/**
 * @brief   free a heavy keeper query result.
 * @param   stats_hd     the pointer to the heavy keeper query result.
 */
void heavy_keeper_result_free(struct heavy_keeper_result *stats_hd);

/**
 * @brief   Merge two heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   hk_merged     the pointer to the heavy keeper to be merged. Must have the same params with hk.
 * @returns 0 if success, -1 if failed.
 */
int heavy_keeper_merge(struct heavy_keeper *hk, const struct heavy_keeper *src);

/**
 * @brief   Serialize a heavy keeper.
 * @param   hk            the pointer to the heavy keeper.
 * @param   blob          output of the serialized data. Must be freed by caller. *blob can be NULL, the function will allocate memory for it.
 * @param   blob_sz       Output, the size of the serialized data.
*/
void heavy_keeper_serialization(const struct heavy_keeper *hk, char **blob, size_t *blob_sz);

/**
 * @brief   Deserialize a heavy keeper.
 * @param   blob          the serialized data. From heavy_keeper_serialization.
 * @param   size          the size of the serialized data.
 * @returns a pointer to the heavy keeper.
*/
struct heavy_keeper *heavy_keeper_deserialization(const char *blob, size_t size);

/*
 * @brief   Merge a heavy keeper with serialized info block. Used in CRDT callbacks.
*/
void heavy_keeper_merge_blob(struct heavy_keeper *hk, const char *blob, size_t blob_sz);

/* 
 *@returns the total memory usage of the heavy keeper.
*/
size_t heavy_keeper_cal_total_memory_usage(const struct heavy_keeper *hk);

#ifdef __cplusplus
}
#endif
