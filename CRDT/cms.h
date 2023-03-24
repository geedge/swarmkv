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
struct cms;
struct cms *cms_new(uuid_t my_id);
void cms_free(struct cms *cms);
int cms_check(const struct cms *cms, const char *key, size_t len);
int cms_add_n(struct cms *cms, const char *key, size_t len, int times);
int cms_remove_n(struct cms *cms, const char *key, size_t len, int times);
size_t cms_size(const struct cms *cms);
void cms_serialize(const struct cms *cms, char **blob, size_t *blob_sz);
struct cms *cms_deserialize(const char *blob, size_t blob_sz);
void cms_merge_blob(struct cms *dst, const char *blob, size_t blob_sz);

#ifdef __cplusplus
}
#endif