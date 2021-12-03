#include "inc_internal/swarmkv_conhash.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

int cmp_by_key(const void* a, const void* b)
{
	if(((const struct ch_point*)a)->point_val > ((const struct ch_point*)b)->point_val)
	{
		return 1;
	}
	else if(((const struct ch_point*)a)->point_val == ((const struct ch_point*)b)->point_val)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}
int cmp_by_bucket(const void* a, const void* b)
{
	if(((const struct ch_point*)a)->node_id > ((const struct ch_point*)b)->node_id)
	{
		return 1;
	}
	else if(((const struct ch_point*)a)->node_id == ((const struct ch_point*)b)->node_id)
	{
		return 0;
	}
	else
	{
		return -1;
	}
	
}

void *swarmkv_conhash_create(struct swarmkv_node *nodes, uint32_t points_per_bucket)
{
	struct conhash* ch=NULL;
	int node_num = 0;
	struct swarmkv_node *node = NULL;
	struct swarmkv_node *node_tmp = NULL;	
	HASH_ITER(hh, nodes, node, node_tmp)
	{
		node_num++;
	}
	//int node_num = HASH_COUNT(nodes);
	int i=0,j=0;
	uint32_t randval;
	ch			   = (struct conhash *)calloc( sizeof( struct conhash ), 1 );
	ch->point	   = (struct ch_point*)calloc( sizeof( struct ch_point ), node_num * points_per_bucket);
	//ch			   = (struct conhash *)calloc( 1, sizeof( struct conhash ) );
	//ch->point	   = (struct ch_point*)calloc( node_num * points_per_bucket, sizeof( struct ch_point ) );
	ch->bucket_id_upbound = node_num;
	ch->bucket_cnt = node_num;
	ch->bucket_hit = (uint32_t*)calloc(sizeof(uint32_t), node_num);
	ch->points_per_bucket = points_per_bucket;
	ch->point_num = points_per_bucket*node_num;

	node = NULL;
	node_tmp = NULL;
	HASH_ITER(hh, nodes, node, node_tmp)
	{
		if(node->status != FAIL)
		{
			for(i=0;i<ch->points_per_bucket;i++)
			{
				randval=(((uint32_t)(-1))/ch->point_num)*(i*ch->bucket_id_upbound+node->node_id);
				ch->point[j*ch->bucket_id_upbound+i].node_id=node->node_id;
				ch->point[j*ch->bucket_id_upbound+i].point_val=randval%SWARMKV_DEFAULT_SLOT_NUM;
				ch->point[j*ch->bucket_id_upbound+i].hit_cnt=0;
			}
			j++;
		}
	}
	qsort(ch->point,ch->point_num, sizeof(struct ch_point),cmp_by_key);
	return ch;
}

