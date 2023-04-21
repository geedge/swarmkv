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
	TB_CMD_READ_AVAILABLE,
	TB_CMD_READ_REFILLED,
	TB_CMD_READ_CONSUEMD
};

long long tb_available(long long CIR, long long CBS, long long consumed, long long refilled, long long last_refill_ms, long long now_ms, long long refill_interval_ms, long long *new_refilled);

#ifdef __cplusplus
}
#endif