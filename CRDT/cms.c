#include "cms.h"
#include "uthash.h"
#include "mpack.h"
#include "crdt_utils.h"

#include <stdint.h>
#include <string.h>
#include <assert.h>

#define CMS_MAX_DEPTH   16
static uint64_t __fnv_1a(const char *key, size_t len, int seed) {
    // FNV-1a hash (http://www.isthe.com/chongo/tech/comp/fnv/)
    int i=0;
    uint64_t h = 14695981039346656037ULL + (31 * seed); // FNV_OFFSET 64 bit with magic number seed
    for (i = 0; i < len; ++i){
            h = h ^ (unsigned char) key[i];
            h = h * 1099511628211ULL; // FNV_PRIME 64 bit
    }
    return h;
}
static int32_t __safe_add(int32_t a, int32_t b)
{
    if (a == INT32_MAX || a == INT32_MIN) {
        return a;
    }
    int32_t c=0;
    c = ((int64_t) a + b > INT32_MAX) ? INT32_MAX : (a + b);
    return c;
}
static int32_t __safe_sub(int32_t a, int32_t b)
{
    if (a == INT32_MAX || a == INT32_MIN) {
        return a;
    }
    int32_t c = 0;
    c = ((int64_t) b - a < INT32_MIN) ? INT32_MAX : (a - b);
    return c;
}
struct cms_opt
{
    int width;
    int depth;
};
struct cms_timestamp
{
	uuid_t uuid;
	unsigned long long sequence;
	UT_hash_handle hh;
};
struct cms_bin
{
    uuid_t uuid;
    unsigned long long sequence;
    uint64_t elements_added;
    int width;
    int depth;
    UT_hash_handle hh;
    int32_t *bins;
};
struct cms_bin *cms_bin_new(uuid_t uuid, int width, int depth)
{
    struct cms_bin *bin=NULL;
    bin=ALLOC(struct cms_bin, 1);
    uuid_copy(bin->uuid, uuid);
    bin->width=width;
    bin->depth=depth;
    bin->bins=ALLOC(int32_t, width*depth);
    return bin;
}
static int32_t cms_bin_check(const struct cms_bin *bin, const uint64_t *hashes, size_t n_hash)
{
    if(n_hash!=bin->depth) return 0;

    int32_t added=INT32_MAX;
    for(int i=0; i<bin->depth; i++)
    {
        uint64_t idx=(hashes[i] % bin->width) + (i * bin->width);
        added=MIN(added, bin->bins[idx]);
    }
    return added;
}
static int32_t cms_bin_add(struct cms_bin *bin, const uint64_t *hashes, size_t n_hash, int32_t times)
{
    if(n_hash!=bin->depth) return 0;
    int32_t added=INT32_MAX;
    for(int i=0; i<bin->depth; i++)
    {
        int32_t idx=(hashes[i] % bin->width) + (i * bin->width);
        if(times>0)
        {
            bin->bins[idx] = __safe_add(bin->bins[idx], times);
        }
        else
        {
            bin->bins[idx] = __safe_sub(bin->bins[idx], 0-times);
        }
        added=MIN(added, bin->bins[idx]);
    }
    bin->elements_added += times;
    bin->sequence++;
    return added;
}
void cms_bin_free(struct cms_bin *bin)
{
    free(bin->bins);
    free(bin);
}
size_t cms_bin_blob_size(const struct cms_bin *bin)
{
    return offsetof(struct cms_bin, hh) + sizeof(int32_t)*bin->depth*bin->width;
}
size_t cms_bin_size(const struct cms_bin *bin)
{
   return sizeof(struct cms_bin)+sizeof(int32_t)*bin->depth*bin->width;
}
size_t cms_bin_serialize(const struct cms_bin *bin, char *buff, size_t buff_sz)
{
    size_t offset=0;
    assert(buff_sz>=cms_bin_blob_size(bin));
    memcpy(buff, bin, offsetof(struct cms_bin, hh));
    offset += offsetof(struct cms_bin, hh);
    memcpy(buff+offset, bin->bins, sizeof(int32_t)*bin->depth*bin->width);
    offset += sizeof(int32_t)*bin->depth*bin->width;
    assert(offset==cms_bin_blob_size(bin));
    return offset;
}
struct cms_bin *cms_bin_deserialize(const char *blob, size_t blob_sz)
{
    struct cms_bin *bin=ALLOC(struct cms_bin, 1);
    size_t offset=0;
    memcpy(bin, blob, offsetof(struct cms_bin, hh));
    offset +=  offsetof(struct cms_bin, hh);
    bin->bins=ALLOC(int32_t, bin->depth*bin->width);
    assert(sizeof(int32_t)*bin->depth*bin->width==blob_sz-offset);
    memcpy(bin->bins, blob+offset, sizeof(int32_t)*bin->depth*bin->width);
    return bin;
}
void static cms_bin_merge(struct cms_bin *dst, const struct cms_bin *src)
{
    assert(dst->depth==src->depth);
    assert(dst->width==src->width);
    if(dst->sequence<src->sequence)
    {
        dst->elements_added=src->elements_added;
        dst->sequence=src->sequence;
        memcpy(dst->bins, src->bins, sizeof(int32_t)*dst->width*dst->depth);
    }
    return;
}
struct cms
{
    struct cms_opt opt;
    uuid_t my_uuid;
    struct cms_bin *hash_bins;
};

static const struct cms_opt cms_default_opt = {
    .width=128,
    .depth=8
};
struct cms *cms_new(uuid_t my_id)
{
    struct cms *cms=ALLOC(struct cms, 1);
    uuid_copy(cms->my_uuid, my_id);
    memcpy(&cms->opt, &cms_default_opt, sizeof(cms_default_opt));
    return cms;
}
int cms_check(const struct cms *cms, const char *key, size_t len)
{
    uint64_t hashes[CMS_MAX_DEPTH]={0};
    int added_global=0;
    struct cms_bin *bin=NULL, *tmp=NULL;
    for(int i=0; i<cms->opt.depth && i<CMS_MAX_DEPTH; i++)
    {
        hashes[i]=__fnv_1a(key, len, i);
    }
    HASH_ITER(hh, cms->hash_bins, bin, tmp)
    {
        added_global+=cms_bin_check(bin, hashes, cms->opt.depth);
    }

    return added_global;
}
int cms_add_n(struct cms *cms, const char *key, size_t len, int times)
{
    struct cms_bin *bin=NULL;
    HASH_FIND(hh, cms->hash_bins, cms->my_uuid, sizeof(cms->my_uuid), bin);
    if(!bin)
    {
        bin=cms_bin_new(cms->my_uuid, cms->opt.width, cms->opt.depth);
        HASH_ADD_KEYPTR(hh, cms->hash_bins, bin->uuid, sizeof(bin->uuid), bin);
    }

    uint64_t hashes[CMS_MAX_DEPTH]={0};
    for(int i=0; i<cms->opt.depth && i<CMS_MAX_DEPTH; i++)
	{
        hashes[i]=__fnv_1a(key, len, i);
    }
    cms_bin_add(bin, hashes, cms->opt.depth, times);
    return cms_check(cms, key, len);
}
int cms_remove_n(struct cms *cms, const char *key, size_t len, int times)
{
    return cms_add_n(cms, key, len, 0-times);
}

void cms_free(struct cms *cms)
{
    struct cms_bin *bin=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_bins, bin, tmp)
    {
        HASH_DELETE(hh, cms->hash_bins, bin);
		free(bin->bins);
        free(bin);
    }
    free(cms);
    return;
}

size_t cms_size(const struct cms *cms)
{
	size_t sz=0;
    struct cms_bin *bin=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_bins, bin, tmp)
    {
        sz+=cms_bin_size(bin);
    }
	sz+=sizeof(struct cms);
	return sz;
}
void cms_serialize(const struct cms *cms, char **blob, size_t *blob_sz)
{
    size_t mpack_buff_sz=cms_size(cms)+HASH_COUNT(cms->hash_bins)*sizeof(size_t);
    char *mpack_buff=ALLOC(char, mpack_buff_sz);
    struct cms_bin *bin=NULL, *tmp=NULL;
    size_t offset=0;
    *((size_t*)(mpack_buff+offset))=HASH_COUNT(cms->hash_bins);
    offset+=sizeof(size_t);
    HASH_ITER(hh, cms->hash_bins, bin, tmp)
    {
        *((size_t*)(mpack_buff+offset))=cms_bin_blob_size(bin);
        offset += sizeof(size_t);
        offset += cms_bin_serialize(bin, mpack_buff+offset, mpack_buff_sz-offset);
    }
    *blob=mpack_buff;
    *blob_sz=offset;
    return;
}
struct cms *cms_deserialize(const char *blob, size_t blob_sz)
{
    struct cms *cms=ALLOC(struct cms, 1);
    size_t offset=0, bin_blob_sz=0;
    size_t n_replica = *((size_t*)(blob+offset));
    offset += sizeof(size_t);
    struct cms_bin *bin=NULL;
    for(int i=0; i<n_replica; i++)
    {
        bin_blob_sz=*((size_t*)(blob+offset));
        offset += sizeof(size_t);
        bin=cms_bin_deserialize(blob+offset, bin_blob_sz);
        offset+=bin_blob_sz;
        assert(bin_blob_sz==cms_bin_blob_size(bin));
        HASH_ADD_KEYPTR(hh, cms->hash_bins, bin->uuid, sizeof(bin->uuid), bin);
    }
    return cms;
}

void cms_merge(struct cms *dst, const struct cms *src)
{
    struct cms_bin *src_bin=NULL, *dst_bin=NULL, *tmp=NULL;
	HASH_ITER(hh, src->hash_bins, src_bin, tmp)
	{
		HASH_FIND(hh, dst->hash_bins, src_bin->uuid, sizeof(src_bin->uuid), dst_bin);
		if(!dst_bin)
		{
			dst_bin=cms_bin_new(src_bin->uuid, dst->opt.width, dst->opt.depth);
			HASH_ADD_KEYPTR(hh, dst->hash_bins, dst_bin->uuid, sizeof(dst_bin->uuid), dst_bin);
		}
        cms_bin_merge(dst_bin, src_bin);
	}
}
void cms_merge_blob(struct cms *dst, const char *blob, size_t blob_sz)
{
    struct cms *src=cms_deserialize(blob, blob_sz);
    cms_merge(dst, src);
    cms_free(src);
    return;
}