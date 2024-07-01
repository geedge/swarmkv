#include "crdt_utils.h"
#include "xxhash.h"

// void double_hash_init(struct double_hash *rv, const void *buffer, int len)
// {
//     XXH128_hash_t hash = XXH3_128bits(buffer, len);
//     rv->a = hash.high64;
//     rv->b = hash.low64;
// 	return;
// }
// int double_hash_generate(const struct double_hash *rv, int i, int m)
// {
//     return (rv->a + i * rv->b) % m;
// }