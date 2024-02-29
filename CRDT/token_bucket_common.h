#pragma once
#include <stddef.h>
#include <sys/time.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif

enum tb_consume_type
{
	TB_CONSUME_NORMAL,
	TB_CONSUME_FORCE,
	TB_CONSUME_FLEXIBLE,
	TB_CONSUME_AS_MUCH_AS_POSSIBLE
};
long long tb_available(long long rate, long long period, long long capacity, long long consumed, long long refilled, long long delta_ms, long long refill_interval_ms, long long *new_refilled);
long long tb_local_available(long long rate, long long period, long long available, long long capacity, size_t n_replica);

long long tb_consume_reserve_based(long long rate, long long period, long long available, long long capacity, size_t n_replica, enum tb_consume_type cmd, long long tokens);
long long tb_consume_quantum_based(long long available, long long deficit, long long quantum, enum tb_consume_type cmd, long long tokens);
#ifdef __cplusplus
}
#endif