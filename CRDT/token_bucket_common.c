#include "crdt_utils.h"
#include "token_bucket_common.h"

#include <assert.h>

long long tb_available(long long CIR, long long CBS, long long consumed, long long refilled, long long delta_ms, long long refill_interval_ms, long long *new_refilled)
{

	long long to_add = CIR*delta_ms/1000;
	*new_refilled = refilled;
	if(refilled==0 && consumed==0)
	{
		*new_refilled=CBS;
		return *new_refilled;
	}
	if(delta_ms > refill_interval_ms && to_add>0)
	{
		if(refilled < consumed)
		{
			*new_refilled = consumed;
		}
		if(to_add + refilled - consumed < CBS)
		{
			*new_refilled += to_add;
		}
		else
		{
			*new_refilled += CBS - (refilled - consumed);
		}
	}
	return MAX(*new_refilled-consumed, 0);
}
long long tb_consume_reserve_based(long long CIR, long long available, size_t n_replica, enum tb_consume_type cmd, long long tokens)
{
	long long reserved=CIR*(n_replica-1)/n_replica;
	long long local_available=MAX(available-reserved, 0);
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