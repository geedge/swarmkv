#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#include "oc_token_bucket.h"

#ifdef __cplusplus
extern "C"
{
#endif
#define FSTB_WEIGHT_MAX  20
struct fair_token_bucket;
struct fair_token_bucket *fair_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS);
void fair_token_bucket_free(struct fair_token_bucket *fstb);
long long fair_token_bucket_consume(struct fair_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens);

struct fair_token_bucket_info
{
    struct OC_token_bucket_info bucket_info;
    long long active_classes_number;
};
void fair_token_bucket_info(const struct fair_token_bucket *fstb, struct fair_token_bucket_info *info);

#ifdef __cplusplus
}
#endif