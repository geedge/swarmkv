#include "g_array.h"
#include "crdt_utils.h"

#include "uthash.h"

#include <assert.h>
#define	TO_LOCAL_EPOCH(x)	(x & ((1ULL << 24) - 1))
struct counter_item
{
	long long epoch:24;
	long long count:40;
};
struct counter_array
{
	uuid_t replica_id;
    long long array_sz;
	long long sequence;
	struct counter_item  *array;
	UT_hash_handle hh;
};
static struct counter_array *counter_array_new(uuid_t uuid, long long array_sz)
{
	struct counter_array *a=ALLOC(struct counter_array, 1);
	uuid_copy(a->replica_id, uuid);
	a->array_sz=array_sz;
	a->array=ALLOC(struct counter_item, a->array_sz);
	return a;
}
static void counter_array_free(struct counter_array *a)
{
	free(a->array);
	free(a);
	return;
}
struct g_array	//Grow only array
{
	uuid_t my_id;
	long long array_sz;
	long long epoch;
	struct counter_array *hash;
};
struct g_array *g_array_new(uuid_t my_id, long long array_sz)
{
	struct g_array *ga=ALLOC(struct g_array, 1);
	uuid_copy(ga->my_id, my_id);
	ga->array_sz=array_sz;
    return ga;
}
void g_array_free(struct g_array *ga)
{
	struct counter_array *a=NULL, *tmp=NULL;
	HASH_ITER(hh, ga->hash, a, tmp)
	{
		HASH_DELETE(hh, ga->hash, a);
		counter_array_free(a);
	}
	free(ga);
	return;
}

long long g_array_get(const struct g_array *ga, long long idx)
{
	struct counter_array *a=NULL, *tmp=NULL;
	long long value=0;
	HASH_ITER(hh, ga->hash, a, tmp)
	{
        if(idx < a->array_sz && a->array[idx].epoch == TO_LOCAL_EPOCH(ga->epoch))
            value += a->array[idx].count;
	}
	return value;
}

long long g_array_incrby(struct g_array *ga, long long idx, long long increment)
{
	struct counter_array *a=NULL;
	HASH_FIND(hh, ga->hash, ga->my_id, sizeof(ga->my_id), a);
	if(!a)
	{
		a=counter_array_new(ga->my_id, ga->array_sz);
		HASH_ADD_KEYPTR(hh, ga->hash, a->replica_id, sizeof(a->replica_id), a);
	}
	if(a->array[idx].epoch != TO_LOCAL_EPOCH(ga->epoch))
	{
		a->array[idx].epoch = TO_LOCAL_EPOCH(ga->epoch);
		a->array[idx].count = 0;
	}
	a->array[idx].count += increment;
	a->sequence ++;
	return g_array_get(ga, idx);
}
void g_array_reset(struct g_array *ga)
{
	ga->epoch++;
}
void g_array_resize(struct g_array *ga, long long new_size)
{
	struct counter_array *a=NULL;
	HASH_FIND(hh, ga->hash, ga->my_id, sizeof(ga->my_id), a);
	if(a && a->array_sz < new_size)
	{
        a->array=(struct counter_item *)realloc(a->array, sizeof(struct counter_item)*new_size);
        a->array_sz=new_size;
	}
	ga->array_sz=new_size;
	a->sequence++;
    return;
}
size_t g_array_replicas(const struct g_array *ga)
{
	return HASH_COUNT(ga->hash);
}
const size_t G_ARRAY_ITEM_HEADER_SIZE=offsetof(struct counter_array, array);
size_t g_array_serialized_size(const struct g_array *ga)
{
	size_t sz=0;
	struct counter_array *item=NULL, *tmp=NULL;
	sz += sizeof(long long)*2;
	HASH_ITER(hh, ga->hash, item, tmp)
	{
		sz += G_ARRAY_ITEM_HEADER_SIZE;
        sz += item->array_sz*sizeof(struct counter_item);
	}
    return sz;
}
void g_array_serialize(const struct g_array *ga, char **blob, size_t *blob_sz)
{
	size_t sz=0, offset=0;
	sz=g_array_serialized_size(ga);
	
	char *buffer=ALLOC(char, sz);
	struct counter_array *item=NULL, *tmp=NULL;
	*(long long *)(buffer +offset)=ga->epoch;
	offset += sizeof(long long);
	*(long long *)(buffer +offset)=HASH_COUNT(ga->hash);
	offset += sizeof(long long);
	HASH_ITER(hh, ga->hash, item, tmp)
	{
		memcpy(buffer+offset, item, G_ARRAY_ITEM_HEADER_SIZE);
		offset += G_ARRAY_ITEM_HEADER_SIZE;
		memcpy(buffer+offset, item->array, item->array_sz*sizeof(struct counter_item));
		offset += item->array_sz*sizeof(struct counter_item);
	}
	assert(offset==sz);
	*blob=buffer;
	*blob_sz=sz;
	return;
}
struct g_array * g_array_deserialize(const char *blob, size_t blob_sz)
{
	struct g_array *ga=ALLOC(struct g_array, 1);
	ga->array_sz=0;
	struct counter_array *item=NULL;
	size_t offset=0, n_item=0;
	ga->epoch=*(const long long*)(blob +offset);
	offset += sizeof(long long);
	n_item=*(const long long*)(blob +offset);
	offset += sizeof(long long);
	for(size_t i=0; i<n_item; i++)
	{
		assert(offset<blob_sz);
		item=ALLOC(struct counter_array, 1);
		memcpy(item, blob+offset, G_ARRAY_ITEM_HEADER_SIZE);
		offset += G_ARRAY_ITEM_HEADER_SIZE;
		item->array=ALLOC(struct counter_item, item->array_sz);
		memcpy(item->array, blob+offset, item->array_sz*sizeof(struct counter_item));
		offset += item->array_sz*sizeof(struct counter_item);
		HASH_ADD_KEYPTR(hh, ga->hash, item->replica_id, sizeof(item->replica_id), item);
		
	}
	assert(offset<=blob_sz);
	return ga;
}
void g_array_merge(struct g_array *dst, const struct g_array *src)
{
    struct counter_array *src_item=NULL, *dst_item=NULL, *tmp=NULL;
    HASH_ITER(hh, src->hash, src_item, tmp)
    {
        HASH_FIND(hh, dst->hash, src_item->replica_id, sizeof(src_item->replica_id), dst_item);
        if(!dst_item)
        {
            dst_item=ALLOC(struct counter_array, 1);
			memcpy(dst_item, src_item, G_ARRAY_ITEM_HEADER_SIZE);
            dst_item->array=ALLOC(struct counter_item, dst_item->array_sz);
            memcpy(dst_item->array, src_item->array, dst_item->array_sz*sizeof(struct counter_item));
			HASH_ADD_KEYPTR(hh, dst->hash, dst_item->replica_id, sizeof(dst_item->replica_id), dst_item);
        }
		else
		{
			if(src_item->sequence > dst_item->sequence)
			{
				dst_item->array_sz=src_item->array_sz;
				dst_item->array=realloc(dst_item->array, sizeof(struct counter_item)*dst_item->array_sz);
				memcpy(dst_item->array, src_item->array, sizeof(struct counter_item)*dst_item->array_sz);
			}
		}
    }
	dst->epoch=MAX(dst->epoch, src->epoch);
    return;
}
void g_array_merge_blob(struct g_array *ga, const char *blob, size_t blob_sz)
{
	struct g_array *src=g_array_deserialize(blob, blob_sz);
    g_array_merge(ga, src);
    g_array_free(src);
    return;
}