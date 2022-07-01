#include <sys/time.h>
#include <stddef.h>	//size_t
#include <stdlib.h>	//calloc
#include <string.h>


#ifndef ALLOC
#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#endif

struct lww_register
{
	struct timeval timestamp;
	size_t size;
	char *value;
};

void lww_register_set(struct lww_register *reg, const char *value, size_t size)
{
	reg->value=realloc(reg->value, size);
	reg->size=size;
	memcpy(reg->value, value, size);
	gettimeofday(&reg->timestamp, NULL);
	return;
}
void lww_register_get1(struct lww_register *reg, char **value, size_t *size)
{
	*value=ALLOC(char, reg->size);
	*size=reg->size;
	memcpy(*value, reg->value, reg->size);
	return;
}
void lww_register_get0(struct lww_register *reg, const char **value, size_t *size)
{
	*value=reg->value;
	*size=reg->size;
	return;
}

void lww_register_merge(struct lww_register *reg, const char *blob, size_t blob_sz)
{
	const struct lww_register *to_merge=(struct lww_register *)blob;
	const char *value = blob + sizeof(struct lww_register)- sizeof(char*);	
	if(timercmp(&reg->timestamp, &to_merge->timestamp, <))//Last-Write-Wins
	{
		reg->value=realloc(reg->value, to_merge->size);
		reg->size=to_merge->size;
		memcpy(reg->value, value, to_merge->size);
		memcpy(&reg->timestamp, &to_merge->timestamp, sizeof(reg->timestamp));
	}
	return;
}
void lww_register_serialize(struct lww_register *reg, char **blob, size_t *blob_sz)
{
	*blob_sz=sizeof(struct lww_register)-sizeof(char*)+reg->size;
	*blob=ALLOC(char, *blob_sz);
	memcpy(*blob, reg, *blob_sz-reg->size);
	memcpy(*blob+*blob_sz-reg->size, reg->value, reg->size);
	return;
}
struct lww_register *lww_register_new(const char *value, size_t size)
{
	struct lww_register *reg=ALLOC(struct lww_register, 1);
	if(value)
	{
		lww_register_set(reg, value, size);
	}
	return reg;
}
void lww_register_free(struct lww_register *reg)
{
	free(reg->value);
	reg->value=NULL;
	free(reg);
}

