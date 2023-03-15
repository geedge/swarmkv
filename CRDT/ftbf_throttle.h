#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct ftbf_throttle;
struct ftbf_throttle *ftbf_throttle_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long alloc_tokens);

void ftbf_throttle_configure(struct ftbf_throttle *t, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms);
struct ftbf_throttle_info
{
	long long CIR;
	long long CBS;
	long long refill_duration_ms;
	long long consumed;
	long long refilled;
	long long available;
	long long number_of_consumers;
};
void ftbf_throttle_info(struct ftbf_throttle *t, struct ftbf_throttle_info *info);
void ftbf_throttle_free(struct ftbf_throttle *t);

enum ftbf_throttle_command
{
	FTBF_CMD_CONSUME_NORMAL,
	FTBF_CMD_CONSUME_FLEXIBLE,
};
long long ftbf_throttle_control(struct ftbf_throttle *t, struct timeval now, enum ftbf_throttle_command cmd, long long tokens, const char *req_class_id, unsigned char weight);

void ftbf_throttle_serialize(struct ftbf_throttle *t, char **blob, size_t *blob_sz);
struct ftbf_throttle *ftbf_throttle_deserialize(const char *blob, size_t blob_sz);

void ftbf_throttle_merge(struct ftbf_throttle *src, struct ftbf_throttle *dst);
void ftbf_throttle_merge_blob(struct ftbf_throttle *t,  const char *blob, size_t blob_sz);

#ifdef __cplusplus
}
#endif

