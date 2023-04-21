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
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
// Ensure precision in a sane bound
#define HLL_MIN_PRECISION 4      // 16 registers
#define HLL_MAX_PRECISION 18     // 262,144 registers

struct ST_hyperloglog;
struct ST_hyperloglog *ST_hyperloglog_new(unsigned char precision, int time_window_seconds, const struct timeval now);
void ST_hyperloglog_free(struct ST_hyperloglog *h);
//Return 1 if at least 1 ST HyperLogLog internal register was altered. 0 otherwise.
int ST_hyperloglog_add(struct ST_hyperloglog *h, const char *key, size_t keylen, const struct timeval now);
double ST_hyperloglog_count(const struct ST_hyperloglog *h);
size_t ST_hyperloglog_serialized_size(const struct ST_hyperloglog *h);
void ST_hyperloglog_serialize(const struct ST_hyperloglog *h, char **blob, size_t *blob_sz);
struct ST_hyperloglog *ST_hyperloglog_deserialize(const char *blob, size_t blob_sz);
void ST_hyperloglog_merge(struct ST_hyperloglog *dst, const struct ST_hyperloglog *src);
void ST_hyperloglog_merge_blob(struct ST_hyperloglog *dst, const char *blob, size_t blob_sz);
double ST_hyperloglog_error_for_precision(unsigned char precision);
void ST_hyperloglog_configure(struct ST_hyperloglog *h, unsigned char precision, int time_window_seconds, const struct timeval now);
#ifdef __cplusplus
}
#endif