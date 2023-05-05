/*
* A CRDT Count-Min Sketch implementation in C.
* Cormode, Graham; S. Muthukrishnan (2005). "An Improved Data Stream Summary: The Count-Min Sketch and its Applications"
* Author: contact@zhengchao.io
* Count-Min Sketch is a probabilistic data-structure that takes sub linear space to store the probable count, 
* or frequency, of occurrences of elements added into the data-structure.
*/

#pragma once
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct CM_sketch;
struct CM_sketch *CM_sketch_new(uuid_t my_id);
void CM_sketch_free(struct CM_sketch *cms);
int CM_sketch_query(const struct CM_sketch *cms, const char *key, size_t len);
int CM_sketch_add_n(struct CM_sketch *cms, const char *key, size_t len, int times);
int CM_sketch_remove_n(struct CM_sketch *cms, const char *key, size_t len, int times);
size_t CM_sketch_size(const struct CM_sketch *cms);
void CM_sketch_serialize(const struct CM_sketch *cms, char **blob, size_t *blob_sz);
struct CM_sketch *CM_sketch_deserialize(const char *blob, size_t blob_sz);
void CM_sketch_merge_blob(struct CM_sketch *dst, const char *blob, size_t blob_sz);
struct CM_sketch_info
{
    int width;
    int depth;
    double error_rate;
    double confidence;
    long long n_element;
    long long n_replica;
};
void CM_sketch_info(const struct CM_sketch *cms, struct CM_sketch_info *info);
int CM_sketch_elements(const struct CM_sketch *cms);
int CM_sketch_max(const struct CM_sketch *cms);
int CM_sketch_min(const struct CM_sketch *cms);
#ifdef __cplusplus
}
#endif