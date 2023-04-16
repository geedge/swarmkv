/*
* Bulk Token Bucket CRDT. A bulk version of OC token bucket.
* Author: contact@zhengchao.io
* 2023-4-14
*/
#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct bulk_token_bucket *bulk_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS, long long bucket_num);
void bulk_token_bucket_free(struct bulk_token_bucket *btb);

enum bulk_token_bucket_command
{
	BTB_CMD_CONSUME_NORMAL,
	BTB_CMD_CONSUME_FORCE,
	BTB_CMD_CONSUME_FLEXIBLE,
	BTB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE,
	BTB_CMD_READ_AVAILABLE,
	BTB_CMD_READ_REFILLED,
	BTB_CMD_READ_CONSUEMD
};
long long bulk_token_bucket_control(struct bulk_token_bucket *btb, const char *key, size_t key_len, struct timeval now, enum bulk_token_bucket_command cmd, long long tokens);
struct bulk_token_bucket_info
{
	long long CIR;
	long long CBS;
	long long bucket_number;
	long long estimate_keys;
	long long replicas;
	double collision_rate;
};
void bulk_token_bucket_info(const struct bulk_token_bucket *btb, struct bulk_token_bucket_info *info);
#ifdef __cplusplus
}
#endif
