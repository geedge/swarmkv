/*
* Staggered HyperLogLog CRDT
* ST HLL Reference: Cornacchia, Alessandro, et al. "Staggered HLL: Near-continuous-time cardinality estimation with no overhead." 
* Computer Communications 193 (2022): 168-175.
* https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407
* HLL Reference: HyperLogLog in Practice: Algorithmic Engineering of a State of The Art Cardinality Estimation Algorithm
* https://storage.googleapis.com/pub-tools-public-publication-data/pdf/40671.pdf
* The HyperLogLog implementation is adapted from armon/hlld (https://github.com/armon/hlld/blob/master/src/hll.c)
* Author: contact@zhengchao.io
*/
#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <stdint.h>
#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C"
{
#endif
// Ensure precision in a sane bound
#define HLL_MIN_PRECISION 4      // 16 registers
#define HLL_MAX_PRECISION 18     // 262,144 registers

struct ST_hyperloglog;
/** ***************************************************************************
 * Create a Staggered HyperLogLog (STHLL) that can estimate cardinality of a sliding twindow.
 * 
 * Parameters:
 * -----------
 *     precision - The precision of the HyperLogLog. The number of registers is 2^precision. The value should be between 4 to 18.
 *     time_window_ms - The duration of the time window in milliseconds.
 *     now - The current time.
*
 * Return:
 * -------
 *     Pointer to the created STHLL - on success
 *     NULL - on failure
 *
 */
struct ST_hyperloglog *ST_hyperloglog_new(unsigned char precision, long long time_window_ms, const struct timeval now);
void ST_hyperloglog_free(struct ST_hyperloglog *h);
//The parameter 'uuid' is included for consistency with other CRDT types and is not used.
struct ST_hyperloglog *ST_hyperloglog_replicate(uuid_t uuid, const char *blob, size_t blob_sz);
struct ST_hyperloglog_info
{
    long long time_window_ms;
    double error;
    unsigned char precision;
};
void ST_hyperloglog_info(const struct ST_hyperloglog *h, struct ST_hyperloglog_info *info);
//Return 1 if at least 1 ST HyperLogLog internal register was altered. 0 otherwise.
int ST_hyperloglog_add(struct ST_hyperloglog *h, const char *key, size_t keylen, const struct timeval now);
int ST_hyperloglog_add_hash(struct ST_hyperloglog *h, uint64_t hash, const struct timeval now);
void ST_hyperloglog_step(struct ST_hyperloglog *h, const struct timeval now);
double ST_hyperloglog_count(const struct ST_hyperloglog *h, struct timeval now);
size_t ST_hyperloglog_serialized_size(const struct ST_hyperloglog *h);
void ST_hyperloglog_serialize(const struct ST_hyperloglog *h, char **blob, size_t *blob_sz);
struct ST_hyperloglog *ST_hyperloglog_deserialize(const char *blob, size_t blob_sz);
void ST_hyperloglog_merge(struct ST_hyperloglog *dst, const struct ST_hyperloglog *src);
void ST_hyperloglog_merge_blob(struct ST_hyperloglog *dst, const char *blob, size_t blob_sz);
double ST_hyperloglog_error_for_precision(unsigned char precision);
void ST_hyperloglog_configure(struct ST_hyperloglog *h, unsigned char precision, int time_window_seconds, const struct timeval now);
size_t ST_hyperloglog_mem_size(const struct ST_hyperloglog *h);
#ifdef __cplusplus
}
#endif