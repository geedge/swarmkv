/*
* A naive Last-Write-Wins Register
* Simply reply on global clock synchronization
* Author: contact@zhengchao.io
*/


#pragma once
#include <stddef.h>
#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C"
{
#endif
struct LWW_register;
struct LWW_register *LWW_register_new(uuid_t uuid);
void LWW_register_free(struct LWW_register *reg);

void LWW_register_set(struct LWW_register *reg, const char *value, size_t size);
void LWW_register_get1(struct LWW_register *reg, char **value, size_t *size);
void LWW_register_get0(struct LWW_register *reg, const char **value, size_t *size);


void LWW_register_serialize(struct LWW_register *reg, char **blob, size_t *blob_sz);
struct LWW_register *LWW_register_deserialize(const char *blob, size_t blob_sz);
void LWW_register_merge(struct LWW_register *dst, struct LWW_register *src);

void LWW_register_merge_blob(struct LWW_register *reg, const char *blob, size_t blob_sz);
size_t LWW_regeister_mem_size(const struct LWW_register *reg);

#ifdef __cplusplus
}
#endif

