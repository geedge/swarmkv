#include <sys/time.h>
#include <stddef.h>	//size_t
#include <stdlib.h>	//calloc
#include <string.h>


#ifndef ALLOC
#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#endif

struct LWW_register
{
	struct timeval timestamp;
	size_t size;
	char *value;
};

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

void LWW_register_merge(struct LWW_register *reg, const char *blob, size_t blob_sz)
{
	const struct LWW_register *to_merge=(struct LWW_register *)blob;
	const char *value = blob + sizeof(struct LWW_register)- sizeof(char*);	
	if(timercmp(&reg->timestamp, &to_merge->timestamp, <))//Last-Write-Wins
	{
		reg->value=realloc(reg->value, to_merge->size);
		reg->size=to_merge->size;
		memcpy(reg->value, value, to_merge->size);
		memcpy(&reg->timestamp, &to_merge->timestamp, sizeof(reg->timestamp));
	}
	return;
}
void LWW_register_serialize(struct LWW_register *reg, char **blob, size_t *blob_sz)
{
	*blob_sz=sizeof(struct LWW_register)-sizeof(char*)+reg->size;
	*blob=ALLOC(char, *blob_sz);
	memcpy(*blob, reg, *blob_sz-reg->size);
	memcpy(*blob+*blob_sz-reg->size, reg->value, reg->size);
	return;
}
struct LWW_register *LWW_register_new(const char* id, size_t id_sz)
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

