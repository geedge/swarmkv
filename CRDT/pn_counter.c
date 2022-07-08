#include "uthash.h"
#include "mpack.h"
#include <assert.h>
#include <string.h>

#ifndef MAX
#define MAX(a, b)  	(((a) > (b)) ? (a) : (b))
#endif

#ifndef ALLOC
#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#endif

struct counter_item
{
	char* node_id;
	size_t node_id_sz;
	long long P;
	long long N;
	UT_hash_handle hh;
};
struct counter_item *counter_item_new(const char *node_id, size_t node_id_sz)
{
	struct counter_item *item=ALLOC(struct counter_item, 1);
	item->node_id_sz=node_id_sz;
	item->node_id=ALLOC(char, item->node_id_sz);
	memcpy(item->node_id, node_id, item->node_id_sz);
	return item;
}
void counter_item_free(struct counter_item *item)
{
	free(item->node_id);
	item->node_id=NULL;
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
	struct counter_item *c=NULL, *tmp=NULL;
	HASH_ITER(hh, pnc->hash_item, c, tmp)
	{
		abs_positive_counter+=c->P;
		abs_negtive_counter+=c->N;
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
		HASH_ADD_KEYPTR(hh, pnc->hash_item, c->node_id, c->node_id_sz, c);
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
		mpack_write_str(&writer, c->node_id, c->node_id_sz);
		mpack_write_i64(&writer, c->P);
		mpack_write_i64(&writer, c->N);
	}
	mpack_complete_array(&writer);
	mpack_writer_destroy(&writer);
	
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
	return;
}
void PN_counter_merge(struct PN_counter *pnc, const char *blob, size_t blob_sz)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	mpack_node_t mpnode_node_id, mpnode_P, mpnode_N;
	
	size_t array_sz=mpack_node_array_length(root);
	assert(array_sz%3==0);
	
	struct counter_item *c=NULL;	
	for(size_t i=0; i<array_sz; i+=3)
	{
		mpnode_node_id=mpack_node_array_at(root, i);
		mpnode_P=mpack_node_array_at(root, i+1);
		mpnode_N=mpack_node_array_at(root, i+2);
		
		HASH_FIND(hh, pnc->hash_item, mpack_node_str(mpnode_node_id), mpack_node_strlen(mpnode_node_id), c);
		if(!c)
		{			
			c=counter_item_new( mpack_node_str(mpnode_node_id), mpack_node_strlen(mpnode_node_id));
			HASH_ADD_KEYPTR(hh, pnc->hash_item, c->node_id, c->node_id_sz, c);
		}
		c->P=MAX(c->P, mpack_node_i64(mpnode_P));
		c->N=MAX(c->N, mpack_node_i64(mpnode_N));
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred decoding the PN_counter!\n");
	}

	return;
}

