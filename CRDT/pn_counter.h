/*
* State-based PN (Positive-Negative) CRDT counter.
* Author: contact@zhengchao.io
* 
*/

#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct PN_counter;
struct PN_counter *PN_counter_new(const char* id, size_t id_sz);
void PN_counter_free(struct PN_counter *pnc);

long long PN_counter_get(struct PN_counter *pnc);
long long PN_counter_reset(struct PN_counter *pnc);

long long PN_counter_incrby(struct PN_counter *pnc, long long increment);

void PN_counter_merge(struct PN_counter *pnc, const char *blob, size_t blob_sz);
void PN_counter_serialize(struct PN_counter *pnc, char **blob, size_t *blob_sz);

#ifdef __cplusplus
}
#endif

