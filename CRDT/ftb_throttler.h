/*
* Fair Token Bucket Throttler CRDT. 
* 2023-3-15
*/
#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct ftb_throttler;
struct ftb_throttler *ftb_throttler_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long alloc_tokens);

void ftb_throttler_configure(struct ftb_throttler *t, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms);
struct ftb_throttler_info
{
	long long CIR;
	long long CBS;
	long long refill_duration_ms;
	long long consumed;
	long long refilled;
	long long available;
	long long number_of_consumers;
};
void ftb_throttler_info(struct ftb_throttler *t, struct ftb_throttler_info *info);
void ftb_throttler_free(struct ftb_throttler *t);

enum ftb_throttler_command
{
	FTBT_CMD_CONSUME_NORMAL,
	FTBT_CMD_CONSUME_FLEXIBLE,
};
long long ftb_throttler_control(struct ftb_throttler *t, struct timeval now, enum ftb_throttler_command cmd, long long tokens, unsigned long req_class_id, unsigned char weight);

void ftb_throttler_serialize(struct ftb_throttler *t, char **blob, size_t *blob_sz);
struct ftb_throttler *ftb_throttler_deserialize(const char *blob, size_t blob_sz);

void ftb_throttler_merge(struct ftb_throttler *src, struct ftb_throttler *dst);
void ftb_throttler_merge_blob(struct ftb_throttler *t,  const char *blob, size_t blob_sz);

#ifdef __cplusplus
}
#endif

