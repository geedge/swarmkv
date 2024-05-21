#include "crdt_utils.h"
#include "xxhash.h"

void double_hash_init(struct double_hash *rv, const void *buffer, int len)
{
    // rv->a=XXH3_64bits_withSeed(buffer, len, 0x9747b28c);
    // rv->b=XXH3_64bits_withSeed(buffer, len, rv->a);
    XXH128_hash_t xxh128b = XXH3_128bits_withSeed(buffer, len, 0x9747b28c);
    rv->a = xxh128b.high64;
    rv->b = xxh128b.low64;
	return;
}