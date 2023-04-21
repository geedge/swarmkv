#include "crdt_utils.h"

long long tb_available(long long CIR, long long CBS, long long consumed, long long refilled, long long last_refill_ms, long long now_ms, long long refill_interval_ms, long long *new_refilled)
{
	long long eplapse_ms = now_ms - last_refill_ms;
	long long to_add = CIR*eplapse_ms/1000;
	*new_refilled = refilled;
	if(refilled==0 && consumed==0)
	{
		*new_refilled=CBS;
		return *new_refilled;
	}
	if(eplapse_ms > refill_interval_ms && to_add>0)
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