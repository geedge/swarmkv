/*
* Bulk Token Bucket CRDT. A bulk version of OC token bucket.
* Author: contact@zhengchao.io
* 2023-4-14
*/
#pragma once
#include "token_bucket_common.h"
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct bulk_token_bucket *bulk_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long bucket_num);
void bulk_token_bucket_free(struct bulk_token_bucket *btb);
void bulk_token_bucket_configure(struct bulk_token_bucket *btb, struct timeval now, long long CIR, long long CBS, long long bucket_num);

long long bulk_token_bucket_consume(struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t key_len, enum tb_consume_type cmd, long long tokens);
long long bulk_token_bucket_read_available(const struct bulk_token_bucket *btb, struct timeval now, const char *key, size_t keylen);
struct bulk_token_bucket_info
{
	long long CIR;
	long long CBS;
	long long bucket_number;
	long long estimate_keys;
	long long replicas;
	double collision_rate;
};
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct timeval now, struct bulk_token_bucket_info *info);
void bulk_token_bucket_serialize(const struct bulk_token_bucket *btb, char **blob, size_t *blob_sz);
struct bulk_token_bucket *bulk_token_bucket_deserialize(const char *blob, size_t blob_sz);
void bulk_token_bucket_merge(struct bulk_token_bucket *dst, const struct bulk_token_bucket *src);
void bulk_token_bucket_merge_blob(struct bulk_token_bucket *btb, const char * blob, size_t blob_sz);
size_t bulk_token_bucket_mem_size(const struct bulk_token_bucket *btb);
#ifdef __cplusplus
}
#endif
