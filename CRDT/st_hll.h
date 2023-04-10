/*
* Staggered HyperLogLog CRDT
* Reference: Cornacchia, Alessandro, et al. "Staggered HLL: Near-continuous-time cardinality estimation with no overhead." 
* Computer Communications 193 (2022): 168-175.
* https://www.sciencedirect.com/science/article/abs/pii/S0140366422002407
* The HyperLogLog implementation is adapted from armon/hlld (https://github.com/armon/hlld/blob/master/src/hll.c)
* * Author: contact@zhengchao.io
*/
#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct ST_HLL;
struct ST_HLL *ST_HLL_new(unsigned char precision, int time_window_seconds, const struct timeval *now);
void ST_HLL_free(struct ST_HLL *h);
void ST_HLL_add(struct ST_HLL *h, const char *key, size_t keylen, const struct timeval *now);
double ST_HLL_count(const struct ST_HLL *h);
double HLL_error_for_precision(unsigned char precision);
#ifdef __cplusplus
}
#endif