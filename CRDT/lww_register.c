#include "crdt_utils.h"

#include <sys/time.h>
#include <stddef.h>	//size_t
#include <string.h>
#include <assert.h>
#include <uuid/uuid.h>
struct LWW_register
{
	struct timeval timestamp;
	size_t size;
	char *value;
};
struct LWW_register *LWW_register_new(uuid_t uuid)
{
	struct LWW_register *reg=ALLOC(struct LWW_register, 1);
	return reg;
}
void LWW_register_free(struct LWW_register *reg)
{
	free(reg->value);
	reg->value=NULL;
	free(reg);
}

void LWW_register_set(struct LWW_register *reg, const char *value, size_t size)
{
	reg->value=realloc(reg->value, size);
	reg->size=size;
	memcpy(reg->value, value, size);
	gettimeofday(&reg->timestamp, NULL);
	return;
}
void LWW_register_get1(struct LWW_register *reg, char **value, size_t *size)
{
	*value=ALLOC(char, reg->size);
	*size=reg->size;
	memcpy(*value, reg->value, reg->size);
	return;
}
void LWW_register_get0(struct LWW_register *reg, const char **value, size_t *size)
{
	*value=reg->value;
	*size=reg->size;
	return;
}
void LWW_register_merge(struct LWW_register *dst, struct LWW_register *src)
{
	if(timercmp(&dst->timestamp, &src->timestamp, <))//Last-Write-Wins
	{
		dst->value=realloc(dst->value, src->size);
		dst->size=src->size;
		memcpy(dst->value, src->value, src->size);
		memcpy(&dst->timestamp, &src->timestamp, sizeof(dst->timestamp));
	}	
	return;
}

struct LWW_register *LWW_register_deserialize(const char *blob, size_t blob_sz)
{
	struct LWW_register *to_merge=ALLOC(struct LWW_register, 1);
	size_t header_size= sizeof(struct LWW_register)- sizeof(char*);
	memcpy(to_merge, blob, sizeof(struct LWW_register)- sizeof(char*));
	assert(to_merge->size+header_size == blob_sz);
	to_merge->value=ALLOC(char, to_merge->size);
	memcpy(to_merge->value, blob + header_size, to_merge->size);
	return to_merge;
}
void LWW_register_serialize(struct LWW_register *reg, char **blob, size_t *blob_sz)
{
	*blob_sz=sizeof(struct LWW_register)-sizeof(char*)+reg->size;
	*blob=ALLOC(char, *blob_sz);
	memcpy(*blob, reg, *blob_sz-reg->size);
	memcpy(*blob+*blob_sz-reg->size, reg->value, reg->size);
	return;
}
void LWW_register_merge_blob(struct LWW_register *reg, const char *blob, size_t blob_sz)
{
	struct LWW_register *to_merge=LWW_register_deserialize(blob, blob_sz);
	LWW_register_merge(reg, to_merge);
	LWW_register_free(to_merge);
	return;
}



