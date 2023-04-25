/*
* A Fair Split Token Bucket CRDT
* Reference: Unpublished paper, Chao Zheng, "A Stochastic Fair Token Bucket CRDT for Distritubted Rate-limiting"
* The fair token bucket archieves max-min fairness with Deficit-Round-Robin and SFQ.
* - Resources are allocated in order of increasing demand
* - No source gets a resource share larger than its demand
* - Sources with unsatisfied demands get an equal share of the resource
* Author: contact@zhengchao.io
*/

#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#include "oc_token_bucket.h"

#ifdef __cplusplus
extern "C"
{
#endif
#define FAIR_TB_WEIGHT_MAX  20
struct fair_token_bucket;
struct fair_token_bucket *fair_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS, long long divisor);
void fair_token_bucket_free(struct fair_token_bucket *ftb);
long long fair_token_bucket_consume(struct fair_token_bucket *ftb, struct timeval now, const char *key, size_t keylen, long long weight, enum tb_consume_type cmd, long long tokens);
void fair_token_bucket_configure(struct fair_token_bucket *ftb, struct timeval now, long long CIR, long long CBS, long long divisor);
struct fair_token_bucket_info
{
    struct OC_token_bucket_info bucket_info;
    long long active_key_number;
    long long divisor;
};
void fair_token_bucket_info(const struct fair_token_bucket *ftb, struct timeval now, struct fair_token_bucket_info *info);

void fair_token_bucket_serialize(const struct fair_token_bucket *ftb, char **blob, size_t *blob_sz);
struct fair_token_bucket *fair_token_bucket_deserialize(const char *blob, size_t blob_sz);
void fair_token_bucket_merge(struct fair_token_bucket *dst, const struct fair_token_bucket *src);
void fair_token_bucket_merge_blob(struct fair_token_bucket *ftb, const char *blob, size_t blob_sz);
size_t fair_token_bucket_mem_size(const struct fair_token_bucket *ftb);
#ifdef __cplusplus
}
#endif