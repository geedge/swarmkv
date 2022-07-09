/*
* A naive Last-Write-Wins Register
* Simply reply on global clock synchronization
* Author: contact@zhengchao.io
*/


#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif
struct lww_register;
struct lww_register *lww_register_new(const char *value, size_t size);
void lww_register_free(struct lww_register *reg);

void lww_register_set(struct lww_register *reg, const char *value, size_t size);
void lww_register_get1(struct lww_register *reg, char **value, size_t *size);
void lww_register_get0(struct lww_register *reg, const char **value, size_t *size);


void lww_register_serialize(struct lww_register *reg, char **blob, size_t *blob_sz);
void lww_register_merge(struct lww_register *reg, const char *blob, size_t blob_sz);


#ifdef __cplusplus
}
#endif

