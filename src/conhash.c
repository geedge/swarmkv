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

int cmp_by_node_id (const void * a, const void * b)
{
  return ( *(int*)a - *(int*)b );
}


void *swarmkv_conhash_create(struct swarmkv_node *nodes, uint32_t points_per_bucket)
{
	struct conhash* ch=NULL;
	int node_num = HASH_COUNT(nodes);
	uint32_t randval;
	ch			   = (struct conhash *)calloc( sizeof( struct conhash ), 1 );
	ch->point	   = (struct ch_point*)calloc( sizeof( struct ch_point ), node_num * points_per_bucket);
	ch->bucket_id_upbound = node_num;
	ch->bucket_cnt = node_num;
	ch->bucket_hit = (uint32_t*)calloc(sizeof(uint32_t),node_num);
	ch->points_per_bucket = points_per_bucket;
	ch->point_num = points_per_bucket*node_num;

	/*
	struct swarmkv_node *nd;
	for(nd = nodes; nd!=NULL; nd = (struct swarmkv_node *)(nd->hh.next))
	{
		int i=0;
		for(i=0;i<ch->points_per_bucket;i++)
		{
			randval=(((uint32_t)(-1))/ch->point_num)*(i*ch->bucket_id_upbound+nd->node_id);
            ch->point[j*ch->bucket_id_upbound+i].node_id=nd->node_id;
            ch->point[j*ch->bucket_id_upbound+i].point_val=randval%SWARMKV_DEFAULT_SLOT_NUM;
			ch->point[j*ch->bucket_id_upbound+i].hit_cnt=0;
		}
		j++;
	}
	*/
	struct swarmkv_node *node = NULL;
	struct swarmkv_node *node_tmp = NULL;
	int NID[node_num];
	int nd = 0;
	HASH_ITER(hh, nodes, node, node_tmp)
	{
		NID[nd] = node->node_id;
		nd++;
	}
	qsort (NID, node_num, sizeof(int), cmp_by_node_id);
	
	int num = 0;
	for(nd=0; nd<node_num; nd++)
	{
		for(int i=0; i<ch->points_per_bucket; i++)
		{	
			randval = (((uint32_t)(-1))/ch->point_num)*(i * ch->bucket_id_upbound + NID[nd]);
            ch->point[num].node_id = NID[nd];
            ch->point[num].point_val = randval % SWARMKV_DEFAULT_SLOT_NUM;
			ch->point[num].hit_cnt = 0;
			num++;
		}
	}
	qsort(ch->point,ch->point_num, sizeof(struct ch_point),cmp_by_key);
	return ch;
}

