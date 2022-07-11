#include "crdt_utils.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>



struct counter_item
{
	char* replica_id;
	size_t replica_id_sz;
	long long P;
	long long N;
	UT_hash_handle hh;
};
struct counter_item *counter_item_new(const char *replica_id, size_t replica_id_sz)
{
	struct counter_item *item=ALLOC(struct counter_item, 1);
	item->replica_id_sz=replica_id_sz;
	item->replica_id=ALLOC(char, item->replica_id_sz);
	memcpy(item->replica_id, replica_id, item->replica_id_sz);
	return item;
}
void counter_item_free(struct counter_item *item)
{
	free(item->replica_id);
	item->replica_id=NULL;
	free(item);
}
struct PN_counter
{
	char* my_id;
	size_t my_id_sz;
	struct counter_item *hash_item;
};
struct PN_counter *PN_counter_new(const char* id, size_t id_sz)
{
	struct PN_counter *pnc=ALLOC(struct PN_counter, 1);
	pnc->my_id_sz=id_sz;
	pnc->my_id=ALLOC(char, pnc->my_id_sz);
	memcpy(pnc->my_id, id, pnc->my_id_sz);
	return pnc;
}
void PN_counter_free(struct PN_counter *pnc)
{
	struct counter_item *item=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		HASH_DELETE(hh, pnc->hash_item, item);
		counter_item_free(item);
	}
	free(pnc->my_id);
	free(pnc);
}

long long PN_counter_get(struct PN_counter *pnc)
{
	long long abs_positive_counter=0, abs_negtive_counter=0;
	struct counter_item *item=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		abs_positive_counter+=item->P;
		abs_negtive_counter+=item->N;
	}
	return abs_positive_counter-abs_negtive_counter;
}


long long PN_counter_incrby(struct PN_counter *pnc, long long increment)
{
	struct counter_item *c=NULL;
	HASH_FIND(hh, pnc->hash_item, pnc->my_id, pnc->my_id_sz, c);
	if(!c)
	{
		c=counter_item_new(pnc->my_id, pnc->my_id_sz);
		HASH_ADD_KEYPTR(hh, pnc->hash_item, c->replica_id, c->replica_id_sz, c);
	}
	if(increment>0)
	{
		c->P += increment;
	}
	else
	{
		c->N -= increment;
	}
	return PN_counter_get(pnc);
}
void PN_counter_reset(struct PN_counter *pnc)
{
	struct counter_item *item=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, item, tmp)
	{
		item->P=MAX(item->P, item->N);
		item->N=MAX(item->P, item->N);
	}
	return;
}

void PN_counter_serialize(struct PN_counter *pnc, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct counter_item *c=NULL, *tmp=NULL;

	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);
	mpack_build_array(&writer);	
	HASH_ITER(hh, pnc->hash_item, c, tmp)
	{
		mpack_write_str(&writer, c->replica_id, c->replica_id_sz);
		mpack_write_i64(&writer, c->P);
		mpack_write_i64(&writer, c->N);
	}
	mpack_complete_array(&writer);
	mpack_writer_destroy(&writer);
	
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
	return;
}
struct PN_counter *PN_counter_deserialize(const char *blob, size_t blob_sz)
{
	struct PN_counter *pnc=ALLOC(struct PN_counter, 1);
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t mpnode_node_id, mpnode_P, mpnode_N;
	
	size_t array_sz=mpack_node_array_length(root);
	assert(array_sz%3==0);
	
	struct counter_item *item=NULL;	
	for(size_t i=0; i<array_sz; i+=3)
	{
		mpnode_node_id=mpack_node_array_at(root, i);
		HASH_FIND(hh, pnc->hash_item, mpack_node_str(mpnode_node_id), mpack_node_strlen(mpnode_node_id), item);
		assert(item==NULL);

		item=counter_item_new( mpack_node_str(mpnode_node_id), mpack_node_strlen(mpnode_node_id));
		mpnode_P=mpack_node_array_at(root, i+1);
		mpnode_N=mpack_node_array_at(root, i+2);
		item->P=mpack_node_i64(mpnode_P);
		item->N=mpack_node_i64(mpnode_N);
		HASH_ADD_KEYPTR(hh, pnc->hash_item, item->replica_id, item->replica_id_sz, item);
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred decoding the PN_counter!\n");
	}
	return pnc;
}
void PN_counter_merge(struct PN_counter *dst, struct PN_counter *src)
{
	struct counter_item *src_item=NULL, *dst_item=NULL, *tmp=NULL;	

	HASH_ITER(hh, src->hash_item, src_item, tmp)
	{
		HASH_FIND(hh, dst->hash_item, src_item->replica_id,  src_item->replica_id_sz, dst_item);
		if(!dst_item)
		{
			dst_item=counter_item_new(src_item->replica_id, src_item->replica_id_sz);
			HASH_ADD_KEYPTR(hh, dst->hash_item, dst_item->replica_id, dst_item->replica_id_sz, dst_item);
		}
		dst_item->P=MAX(dst_item->P, src_item->P);
		dst_item->N=MAX(dst_item->N, src_item->N);
	}
	return;
}
void PN_counter_merge_blob(struct PN_counter *pnc, const char *blob, size_t blob_sz)
{
	struct PN_counter *to_merge=PN_counter_deserialize(blob, blob_sz);
	PN_counter_merge(pnc, to_merge);
	PN_counter_free(to_merge);
	return;
}

