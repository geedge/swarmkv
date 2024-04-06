#include "cm_sketch.h"
#include "uthash.h"
#include "xxhash.h"
#include "mpack.h"
#include "crdt_utils.h"

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h> //pow

struct cms_opt
{
    int width;
    int depth;
};
struct cms_node
{
    uuid_t uuid;
    unsigned long long version;
    uint64_t elements_added;
    int width;
    int depth;
    int *array;
    UT_hash_handle hh;
};
struct cms_node *cms_node_new(uuid_t uuid, int width, int depth)
{
    struct cms_node *node=ALLOC(struct cms_node, 1);
    uuid_copy(node->uuid, uuid);
    node->width=width;
    node->depth=depth;
    node->array=ALLOC(int, width*depth);
    return node;
}
void cms_node_free(struct cms_node *node)
{
    free(node->array);
    free(node);
    return;
}

static void cms_node_incrby(struct cms_node *node, int *hash, int n_hash, int increment)
{
    assert(n_hash==node->depth);
    for(int i=0; i<node->depth; i++)
    {
        assert(hash[i]>=0 && hash[i]<node->width);
        int offset = hash[i] + i*node->width;
        __builtin_add_overflow(node->array[offset], increment, &node->array[offset]);
        //node->array[offset] += increment;
    }
    node->elements_added += increment;
    node->version ++;
    return;
}
static int cms_node_query(const struct cms_node *node, const int *hash, int n_hash)
{
    assert(n_hash==node->depth);
    int min_val=INT32_MAX;
    for(int i=0; i<node->depth; i++)
    {
        assert(hash[i]>=0 && hash[i]<node->width);
        int offset = hash[i] + i*node->width;
        if(min_val>node->array[offset])
        {
            min_val=node->array[offset];
        }
    }
    return min_val;
}
static void cms_node_reset(struct cms_node *node)
{
    memset(node->array, 0, sizeof(int)*node->width*node->depth);
    node->elements_added=0;
    node->version++;
    return;
}
static size_t cms_node_blob_size(const struct cms_node *node)
{
    return offsetof(struct cms_node, hh) + sizeof(int32_t)*node->depth*node->width;
}
static size_t cms_node_size(const struct cms_node *node)
{
   return sizeof(struct cms_node)+sizeof(int32_t)*node->depth*node->width;
}
static size_t cms_node_serialize(const struct cms_node *node, char *buff, size_t buff_sz)
{
    size_t offset=0;
    assert(buff_sz>=cms_node_blob_size(node));
    memcpy(buff, node, offsetof(struct cms_node, hh));
    offset += offsetof(struct cms_node, hh);
    memcpy(buff+offset, node->array, sizeof(int)*node->depth*node->width);
    offset += sizeof(int)*node->depth*node->width;
    assert(offset==cms_node_blob_size(node));
    return offset;
}
static struct cms_node *cms_node_deserialize(const char *blob, size_t blob_sz)
{
    struct cms_node *node=ALLOC(struct cms_node, 1);
    size_t offset=0;
    memcpy(node, blob, offsetof(struct cms_node, hh));
    offset +=  offsetof(struct cms_node, hh);
    node->array = ALLOC(int, node->depth*node->width);
    memcpy(node->array, blob+offset, sizeof(int)*node->depth*node->width);
    return node;
}
static void cms_node_merge(struct cms_node *dst, const struct cms_node *src)
{
    assert(dst->depth==src->depth);
    assert(dst->width==src->width);
    if(dst->version<src->version)
    {
        dst->elements_added=src->elements_added;
        dst->version=src->version;
        memcpy(dst->array, src->array, sizeof(int)*dst->width*dst->depth);
    }
    return;
}
struct CM_sketch
{
    uuid_t my_uuid;
    struct cms_opt opt;
    struct cms_node *hash_nodes;
};
struct CM_sketch *CM_sketch_new(uuid_t my_id, int width, int depth)
{
    struct CM_sketch *cms=ALLOC(struct CM_sketch, 1);
    uuid_copy(cms->my_uuid, my_id);
    cms->opt.width=width;
    cms->opt.depth=depth;
    return cms;
}
static int CM_sketch_query_hash(const struct CM_sketch *cms, const int *hashes, size_t n_hash)
{
    int added_global=0;
    struct cms_node *node=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        added_global += cms_node_query(node, hashes, n_hash);
    }
    return added_global;
}
int CM_sketch_query(const struct CM_sketch *cms, const char *item, size_t len)
{
    struct double_hash dhash;
    double_hash_init(&dhash, item, len);
    int hash[cms->opt.depth];
    for(int i=0; i<cms->opt.depth; i++)
    {
        hash[i]=double_hash_generate(&dhash, i, cms->opt.width);
    }
    return CM_sketch_query_hash(cms, hash, cms->opt.depth);
}
int CM_sketch_incrby(struct CM_sketch *cms, const char *item, size_t len, int increment)
{
    struct double_hash dhash;
    double_hash_init(&dhash, item, len);
    int hash[cms->opt.depth];
    for(int i=0; i<cms->opt.depth; i++)
    {
        hash[i]=double_hash_generate(&dhash, i, cms->opt.width);
    }
    struct cms_node *node=NULL;
    HASH_FIND(hh, cms->hash_nodes, cms->my_uuid, sizeof(cms->my_uuid), node);
    if(!node)
    {
        node=cms_node_new(cms->my_uuid, cms->opt.width, cms->opt.depth);
        HASH_ADD_KEYPTR(hh, cms->hash_nodes, node->uuid, sizeof(node->uuid), node);
    }
    cms_node_incrby(node, hash, cms->opt.depth, increment);
    return CM_sketch_query_hash(cms, hash, cms->opt.depth);
}

void CM_sketch_free(struct CM_sketch *cms)
{
    struct cms_node *node=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        HASH_DELETE(hh, cms->hash_nodes, node);
        cms_node_free(node);
    }
    free(cms);
    return;
}

size_t CM_sketch_size(const struct CM_sketch *cms)
{
	size_t sz=0;
    struct cms_node *node=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        sz += cms_node_size(node);
    }
	sz += sizeof(struct CM_sketch);
	return sz;
}
size_t CM_sketch_blob_size(const struct CM_sketch *cms)
{
    size_t sz=0;
    struct cms_node *node=NULL, *tmp=NULL;
    sz += offsetof(struct CM_sketch, hash_nodes);
    sz += sizeof(size_t);
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        sz += sizeof(size_t);
        sz += cms_node_blob_size(node);
    }
    return sz;
}
void CM_sketch_serialize(const struct CM_sketch *cms, char **blob, size_t *blob_sz)
{
    size_t mpack_buff_sz=CM_sketch_blob_size(cms);
    char *mpack_buff=ALLOC(char, mpack_buff_sz);
    struct cms_node *node=NULL, *tmp=NULL;
    size_t offset=0;
    memcpy(mpack_buff+offset, cms, offsetof(struct CM_sketch, hash_nodes));
    offset += offsetof(struct CM_sketch, hash_nodes);
    *((size_t*)(mpack_buff+offset))=HASH_COUNT(cms->hash_nodes);
    offset += sizeof(size_t);
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        *((size_t*)(mpack_buff+offset))=cms_node_blob_size(node);
        offset += sizeof(size_t);
        offset += cms_node_serialize(node, mpack_buff+offset, mpack_buff_sz-offset);
    }
    *blob=mpack_buff;
    *blob_sz=offset;
    return;
}
struct CM_sketch *CM_sketch_deserialize(const char *blob, size_t blob_sz)
{
    struct CM_sketch *cms=ALLOC(struct CM_sketch, 1);
    size_t offset=0, node_blob_sz=0;
    memcpy(cms, blob+offset, offsetof(struct CM_sketch, hash_nodes));
    offset += offsetof(struct CM_sketch, hash_nodes);
    size_t n_replica = *((size_t*)(blob+offset));
    offset += sizeof(size_t);
    struct cms_node *node=NULL;
    for(int i=0; i<n_replica; i++)
    {
        node_blob_sz= *((size_t*)(blob+offset));
        offset += sizeof(size_t);
        node=cms_node_deserialize(blob+offset, node_blob_sz);
        offset += node_blob_sz;
        assert(node_blob_sz==cms_node_blob_size(node));
        assert(cms->opt.depth==node->depth);
        assert(cms->opt.width==node->width);
        HASH_ADD_KEYPTR(hh, cms->hash_nodes, node->uuid, sizeof(node->uuid), node);
    }
    return cms;
}
void CM_sketch_merge(struct CM_sketch *dst, const struct CM_sketch *src)
{
    struct cms_node *src_node=NULL, *dst_node=NULL, *tmp=NULL;
    assert(dst->opt.width==src->opt.width);
    assert(dst->opt.depth==src->opt.depth);
	HASH_ITER(hh, src->hash_nodes, src_node, tmp)
	{
        assert(src_node->width==src->opt.width);
        assert(src_node->depth==src->opt.depth);
		HASH_FIND(hh, dst->hash_nodes, src_node->uuid, sizeof(src_node->uuid), dst_node);
		if(!dst_node)
		{
			dst_node=cms_node_new(src_node->uuid, src_node->width, src_node->depth);
			HASH_ADD_KEYPTR(hh, dst->hash_nodes, dst_node->uuid, sizeof(dst_node->uuid), dst_node);
		}
        cms_node_merge(dst_node, src_node);
	}
}
void CM_sketch_merge_blob(struct CM_sketch *dst, const char *blob, size_t blob_sz)
{
    struct CM_sketch *src=CM_sketch_deserialize(blob, blob_sz);
    CM_sketch_merge(dst, src);
    CM_sketch_free(src);
    return;
}

/*In the original Count-Min paper, the authors show that the probability that the estimate of an element is between its true value x
 and an upper bound x+em, where m is the number of elements entered into a [Count-Min Sketch], is larger than 1−d
 under two conditions. The first condition is that the width of the sketch is 2/e
. */
void CM_sketch_info(const struct CM_sketch *cms, struct CM_sketch_info *info)
{
    //https://cs.stackexchange.com/questions/44803/what-is-the-correct-way-to-determine-the-width-and-depth-of-a-count-min-sketch
    info->width=cms->opt.width;
    info->depth=cms->opt.depth;
    info->probability = 1 / pow(2, info->depth);
    info->error=2 / (double) info->width;
    info->n_replica=HASH_COUNT(cms->hash_nodes);
    info->count=0;
    struct cms_node *node=NULL, *tmp=NULL;
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        info->count += node->elements_added;
    }
    return;
}
void CM_sketch_list_replica(const struct CM_sketch *cms, uuid_t *replica, size_t *n_replica)
{
    struct cms_node *node=NULL, *tmp=NULL;
    size_t i=0;
    HASH_ITER(hh, cms->hash_nodes, node, tmp)
    {
        uuid_copy(replica[i], node->uuid);
        i++;
    }
    *n_replica=i;
    return;
}
int CM_sketch_clear_replica(struct CM_sketch *cms, uuid_t replica)
{
    struct cms_node *node=NULL;
    // 'sizeof' on array function parameter 'replica' will return size of 'unsigned char *', so we use sizeof(uuid_t) instead.
    HASH_FIND(hh, cms->hash_nodes, replica, sizeof(uuid_t), node);
    if(!node)
    {
        return -1;
    }
    cms_node_reset(node);
    return 0;
}
void CM_sketch_tool_dimension_by_prob(double error, double probability, int *width, int *depth)
{
    assert(error > 0 && error < 1);
    assert(probability > 0 && probability < 1);

    *width = ceil(2 / error);
    *depth = ceil(log10f(probability) / log10f(0.5));
}
struct CM_sketch *CM_sketch_replicate(uuid_t uuid, const char *blob, size_t blob_sz)
{
    struct CM_sketch *cms=CM_sketch_deserialize(blob, blob_sz);
    uuid_copy(cms->my_uuid, uuid);
    return cms;
}