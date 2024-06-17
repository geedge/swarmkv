#include "crdt_utils.h"
#include "token_bucket_common.h"

#include <assert.h>

long long tb_available(long long rate, long long period, long long capacity, long long refill_interval_ms, long long consumed, long long *refilled, long long now_ms, long long *last_refill_ms)
{
	long long delta_ms = now_ms - *last_refill_ms ;
	delta_ms -= delta_ms % refill_interval_ms;
	long long to_add = period?(rate*delta_ms/period/1000):0;
	if(delta_ms > refill_interval_ms && to_add>0)
	{
		if(*refilled < consumed)
		{
			*refilled = consumed;
		}
		if(to_add + *refilled - consumed < capacity)
		{
			*refilled += to_add;
		}
		else
		{
			*refilled += capacity - (*refilled - consumed);
		}
		*last_refill_ms += delta_ms;
	}
	return MAX(*refilled-consumed, 0);
}
long long tb_local_available(long long rate, long long period, long long available, long long capacity, size_t n_replica)
{
	long long reserved = period ? (rate*(n_replica-1)/n_replica/period) : 0;
	reserved = MIN(reserved, capacity*(n_replica-1)/n_replica);
	long long local_available = MAX(available-reserved, 0);
	return local_available;
}
long long tb_consume_reserve_based(long long rate, long long period, long long available, long long capacity, size_t n_replica, enum tb_consume_type cmd, long long tokens)
{
	long long local_available = tb_local_available(rate, period, available, capacity, n_replica);
	long long allocated = 0;
	switch(cmd)
	{
		case TB_CONSUME_AS_MUCH_AS_POSSIBLE:
			allocated = available;
			break;
		case TB_CONSUME_FORCE:
			allocated = tokens;
			break;
		case TB_CONSUME_FLEXIBLE:
			allocated = MIN(tokens, local_available);
			break;
		case TB_CONSUME_NORMAL:
			allocated = (tokens <= local_available) ? tokens:0;			
			break;
		default:
			assert(0);
			break;
	}
    return allocated;
}
long long tb_consume_quantum_based(long long available, long long deficit, long long quantum, enum tb_consume_type cmd, long long tokens)
{
	long long local_available=MIN(available, quantum-deficit);

	long long allocated=0;
	switch(cmd)
	{
		case TB_CONSUME_AS_MUCH_AS_POSSIBLE:
			allocated=available;
			break;
		case TB_CONSUME_FORCE:
			allocated=tokens;
			break;
		case TB_CONSUME_FLEXIBLE:
			allocated=MIN(tokens, local_available);
			break;
		case TB_CONSUME_NORMAL:
			allocated=(tokens<=local_available) ? tokens:0;
			break;
		default:
			assert(0);
			break;
	}
    return allocated;
}