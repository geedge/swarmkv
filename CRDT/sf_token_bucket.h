/*
* A Stochastic Fair Token Bucket CRDT
* Reference: Unpublished paper, Chao Zheng, "A Stochastic Fair Token Bucket CRDT for Distritubted Rate-limiting"
* The SFTB archieves max-min fairness:
* - Resources are allocated in order of increasing demand
* - No source gets a resource share larger than its demand
* - Sources with unsatisfied demands get an equal share of the resource
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
struct SF_token_bucket;
struct SF_token_bucket *SF_token_bucket_new(uuid_t uuid, struct timeval now, int type, long long CIR, long long CBS);
void SF_token_bucket_free(struct SF_token_bucket *sft);
long long SF_token_bucket_consume(struct SF_token_bucket *sft, struct timeval now, long long class_id, long long weight, long long tokens);
void SF_token_bucket_print_prob(struct SF_token_bucket *sft, long long *classes, size_t n_class);

#ifdef __cplusplus
}
#endif