/*
* Observed-Consume Token Bucket CRDT. 
* Author: contact@zhengchao.io
* 2022-9-7
*/
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif
struct OC_token_bucket;

// CIR: Committed Information Rate
// CBS: Committed Burst Size
struct OC_token_bucket *OC_token_bucket_new(const char* id, size_t id_sz, long long CIR, long long CBS);
void OC_token_bucket_free(struct OC_token_bucket *bucket);

long long OC_token_bucket_consume(struct OC_token_bucket *bucket, long long tokens);
long long OC_token_bucket_get_available(struct OC_token_bucket *bucket);

void OC_token_bucket_serialize(struct OC_token_bucket *bucket, char **blob, size_t *blob_sz);
struct OC_token_bucket *OC_token_bucket_deserialize(const char *blob, size_t blob_sz);

void OC_token_bucket_merge(struct OC_token_bucket *dst, struct OC_token_bucket *src);
void OC_token_bucket_merge_blob(struct OC_token_bucket *bucket,  const char *blob, size_t blob_sz);
#ifdef __cplusplus
}
#endif

