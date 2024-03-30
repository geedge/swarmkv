#include "crdt_utils.h"
#include "xxhash.h"

void double_hash_init(struct double_hash *rv, const void *buffer, int len)
{
    rv->a=XXH3_64bits_withSeed(buffer, len, 0x9747b28c);
    rv->b=XXH3_64bits_withSeed(buffer, len, rv->a);
	return;
}
int double_hash_generate(const struct double_hash *rv, int i, int m)
{
    return (rv->a + i * rv->b) % m;
}