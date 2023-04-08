#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct FS_token_bucket;
struct FS_token_bucket *FS_token_bucket_new(uuid_t uuid, struct timeval now, long long CIR, long long CBS);
void FS_token_bucket_free(struct FS_token_bucket *fstb);
long long FS_token_bucket_consume(struct FS_token_bucket *fstb, struct timeval now, long long class_id, long long weight, long long tokens);
#ifdef __cplusplus
}
#endif