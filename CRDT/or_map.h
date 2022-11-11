/*
* Add-Wins Observed-Remove Map that is state-based and has no tombstones. 
* Author: contact@zhengchao.io
*/
#pragma once
#include <stddef.h>
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif

struct OR_map;
struct OR_map *OR_map_new(const char* id, size_t id_sz);
void OR_map_free(struct OR_map *map);
int OR_map_add(struct OR_map *map, const char *key, size_t key_len);
int OR_map_set(struct OR_map *map, const char *key, size_t key_len, const char *value, size_t value_len);
int OR_map_incrby(struct OR_map *map, const char *key, size_t key_len, long long increment, long long *result);
int OR_map_remove(struct OR_map *map, const char *key, size_t key_len);
int OR_map_exisits(struct OR_map *map, const char* key, size_t key_len);
size_t OR_map_cardinality(struct OR_map *map);
size_t OR_map_replica_number(struct OR_map *map);

enum value_type
{
	TYPE_NONE,
	TYPE_STRING,
	TYPE_COUNTER
};
struct OR_map_kv
{
	char *key;
	size_t key_len;
	enum value_type type;
	char *string;
	size_t str_len;
	long long counter;
};
void OR_map_kv_free(struct OR_map_kv *kv);
struct OR_map_kv *OR_map_get(struct OR_map *map, const char *key, size_t key_len);
size_t OR_map_get_all(struct OR_map *map, struct OR_map_kv ***kv_list);
size_t OR_map_keys(struct OR_map *map, struct OR_map_kv ***kv_list);

void OR_map_serialize(struct OR_map *map, char **blob, size_t *blob_sz);
struct OR_map *OR_map_deserialize(const char *blob, size_t blob_sz);
void OR_map_merge(struct OR_map *dst, struct OR_map *src);
void OR_map_merge_blob(struct OR_map *map, const char *blob, size_t blob_sz);


#ifdef __cplusplus
}
#endif

