/*
 * A CRDT version of Age-Partitioned Bloom-Filer.
 * Author: contact@zhengchao.io
 * Based on:
 * [1] Ariel Shtul, Carlos Baquero, and Paulo Sérgio Almeida. "Age-partitioned bloom filters." arXiv preprint arXiv:2001.03147 (2020).
 * [2] Ana Rodrigues, Ariel Shtul, Carlos Baquero, and Paulo Sérgio Almeida. "Time-limited Bloom Filter." Proceedings of the 38th ACM/SIGAPP Symposium on Applied Computing. 2023.
 */
#pragma once

#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct AP_bloom;
/** ***************************************************************************
 * Create a bloom filter that can keep items within a sliding time window.
 * 
 * Parameters:
 * -----------
 *     now - The current time.
 *     capacity - The expected number of items which will be inserted. At least 1000. If the actual number of items exceeds the capacity, the bloom filter will expand.
 *     error_rate   - Probability of collision (as long as the capacity is not exceeded).
 *     time_window_ms - The duration of the time window in milliseconds. Items in time range (now-time_window_ms, now] are kept. If time_window_ms is 0, the time window is disabled.
 *     time_slice_num - The number of time slices within the window, which sets the granularity of timeouts to time_window_ms/time_slice_num.
 *
 * Return:
 * -------
 *     Pointer to the created bloom filter - on success
 *     NULL - on failure
 *
 */
struct AP_bloom *AP_bloom_new(struct timeval now, double error_rate, long long capacity, long long time_window_ms, long long time_slice_num);
void AP_bloom_free(struct AP_bloom *bloom);
struct AP_bloom_info
{
    double error;
    long long capacity;
    long long time_window_ms;
    long long time_slice_num;
    long long hash_num;
    long long total_slice_number; //At least hash_num+time_slice_num, and increased after expansion.
    long long max_expand_times;
    long long approximate_item_num;
    double fill_ratio;
    struct timeval oldest_item_time;
};
void AP_bloom_info(const struct AP_bloom *bloom, struct AP_bloom_info *info);

void AP_bloom_add(struct AP_bloom *bloom, struct timeval now, const char *buffer, int len);

 /* Return:
 * -------
 *     0 - element is not present
 *     1 - element is present (or false positive due to collision)
 */
int AP_bloom_check(const struct AP_bloom *bloom, struct timeval now, const char *buffer, int len);


//Approximate the cardinality of the bloom filter.
long long AP_bloom_cardinality(const struct AP_bloom *bloom);

struct AP_bloom *AP_bloom_deserialize(const char *blob, size_t blob_sz);
void AP_bloom_serialize(const struct AP_bloom *bloom, char **blob, size_t *blob_sz);
void AP_bloom_merge(struct AP_bloom *dst, const struct AP_bloom *src);
void AP_bloom_merge_blob(struct AP_bloom *dst, const char *blob, size_t blob_sz);
//The parameter 'uuid' is included for consistency with other CRDT types and is not used.
struct AP_bloom *AP_bloom_replicate(uuid_t uuid, const char *blob, size_t blob_sz);
size_t AP_bloom_serialized_size(const struct AP_bloom *bloom);
size_t AP_bloom_mem_size(const struct AP_bloom *bloom);

#ifdef __cplusplus
}
#endif