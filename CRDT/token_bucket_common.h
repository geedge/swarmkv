#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif

enum token_bucket_command
{
	TB_CMD_CONSUME_NORMAL,
	TB_CMD_CONSUME_FORCE,
	TB_CMD_CONSUME_FLEXIBLE,
	TB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE,
};
long long tb_available(long long CIR, long long CBS, long long consumed, long long refilled, long long delta_ms, long long refill_interval_ms, long long *new_refilled);
long long tb_consume(long long CIR, long long available, size_t n_replica, enum token_bucket_command cmd, long long tokens);
#ifdef __cplusplus
}
#endif