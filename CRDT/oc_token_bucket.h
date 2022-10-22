/*
* Observed-Consume Token Bucket CRDT. 
* Author: contact@zhengchao.io
* 2022-9-7
*/
#pragma once
#include <stddef.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C"
{
#endif
struct OC_token_bucket;

// CIR: Committed Information Rate
// CBS: Committed Burst Size
struct OC_token_bucket *OC_token_bucket_new(const char* id, size_t id_sz, struct timeval now, long long CIR, long long CBS);

void OC_token_bucket_configure(struct OC_token_bucket *bucket, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms);
struct OC_token_bucket_info
{
	long long CIR;
	long long CBS;
	long long refill_duration_ms;
	long long consumed;
	long long refilled;
	long long available;
	long long number_of_consumers;
};
void OC_token_bucket_info(struct OC_token_bucket *bucket, struct OC_token_bucket_info *info);
void OC_token_bucket_free(struct OC_token_bucket *bucket);

enum OC_token_bucket_command
{
	OCTB_CMD_CONSUME_NORMAL,
	OCTB_CMD_CONSUME_FORCE,
	OCTB_CMD_CONSUME_FLEXIBLE,
	OCTB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE,
	OCTB_CMD_READ_AVAILABLE,
	OCTB_CMD_READ_REFILLED,
	OCTB_CMD_READ_CONSUEMD
};
long long OC_token_bucket_control(struct OC_token_bucket *bucket, struct timeval now, enum OC_token_bucket_command cmd, long long tokens);

void OC_token_bucket_serialize(struct OC_token_bucket *bucket, char **blob, size_t *blob_sz);
struct OC_token_bucket *OC_token_bucket_deserialize(const char *blob, size_t blob_sz);

void OC_token_bucket_merge(struct OC_token_bucket *dst, struct OC_token_bucket *src);
void OC_token_bucket_merge_blob(struct OC_token_bucket *bucket,  const char *blob, size_t blob_sz);
#ifdef __cplusplus
}
#endif

