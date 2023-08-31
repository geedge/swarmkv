/*
* Grow-only counter array.
* Author: contact@zhengchao.io
* 2023-4-17
*/
#pragma once
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct g_array;
struct g_array *g_array_new(uuid_t my_id, long long array_sz);
void g_array_free(struct g_array *ga);
long long g_array_incrby(struct g_array *ga, long long idx, long long increment);
long long g_array_get(const struct g_array *ga, long long idx);
void g_array_resize(struct g_array *ga, long long new_size);
void g_array_reset(struct g_array *ga);
size_t g_array_replicas(const struct g_array *ga);
size_t g_array_serialized_size(const struct g_array *ga);
void g_array_serialize(const struct g_array *ga, char **blob, size_t *blob_sz);
struct g_array *g_array_deserialize(const char *blob, size_t blob_sz);
void g_array_merge(struct g_array *dst, const struct g_array *src);
void g_array_merge_blob(struct g_array *ga, const char *blob, size_t blob_sz);
size_t g_array_mem_size(const struct g_array *ga);
#ifdef __cplusplus
}
#endif
