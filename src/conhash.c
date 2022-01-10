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

uint64_t gen_uniq_rand(const void* base, int nmeb, size_t size, int val_offset)
{
	int i=0;
	uint64_t val=0;
	while(1)
	{
		val=(uint64_t)rand()<<32|rand();
		for(i=0;i<nmeb;i++)
		{
			if(val==*(uint64_t*)((char*)base+size*i+val_offset))
			{
				break;
			}
		}
		if(i==nmeb)
		{
			return val;
		}
	}
}
void *swarmkv_conhash_create(int NID[], int node_num, uint32_t points_per_bucket)
{
	struct conhash* ch=NULL;
	uint32_t randval;
	ch			   = (struct conhash *)calloc( sizeof( struct conhash ), 1 );
	ch->point	   = (struct ch_point*)calloc( sizeof( struct ch_point ), node_num * points_per_bucket);
	ch->bucket_id_upbound = node_num;
	ch->bucket_cnt = node_num;
	ch->bucket_hit = (uint32_t*)calloc(sizeof(uint32_t),node_num);
	ch->points_per_bucket = points_per_bucket;
	ch->point_num = points_per_bucket*node_num;
	
	qsort(NID, node_num, sizeof(int), cmp_by_node_id);

	for(int i=0;i<ch->points_per_bucket;i++)
    {
        for(int j=0;j<ch->bucket_id_upbound;j++)
        {
			//randval=(((uint32_t)(-1))/(ch->points_per_bucket*(NID[j]+1)*(i+1)))*((i+1)*(NID[j]*(i+1))*ch->points_per_bucket+SWARMKV_DEFAULT_SLOT_NUM);
			randval=(((uint32_t)(-1))/(ch->points_per_bucket*(NID[j]+i+1)))*((i+1)*(NID[j]+i)*ch->points_per_bucket+SWARMKV_DEFAULT_SLOT_NUM);
            ch->point[i*ch->bucket_id_upbound+j].node_id=NID[j];
            ch->point[i*ch->bucket_id_upbound+j].point_val=randval % SWARMKV_DEFAULT_SLOT_NUM;
			ch->point[i*ch->bucket_id_upbound+j].hit_cnt=0;
		}
	}
	qsort(ch->point,ch->point_num, sizeof(struct ch_point),cmp_by_key);
	return ch;
}
/*
void *swarmkv_conhash_create(struct swarmkv_node *nodes, int node_num, uint32_t points_per_bucket)
{
	struct conhash* ch=NULL;
	//int node_num = HASH_COUNT(nodes);
	uint32_t randval;
	ch			   = (struct conhash *)calloc( sizeof( struct conhash ), 1 );
	ch->point	   = (struct ch_point*)calloc( sizeof( struct ch_point ), node_num * points_per_bucket);
	ch->bucket_id_upbound = node_num;
	ch->bucket_cnt = node_num;
	ch->bucket_hit = (uint32_t*)calloc(sizeof(uint32_t),node_num);
	ch->points_per_bucket = points_per_bucket;
	ch->point_num = points_per_bucket*node_num;

	struct swarmkv_node *node = NULL;
	struct swarmkv_node *node_tmp = NULL;
	int NID[node_num];
	int nd = 0;
	
	HASH_ITER(hh, nodes, node, node_tmp)
	{
		if(node->status != WAITMEET && node->status != FAILED)
		{
			NID[nd] = node->node_id;
			nd++;
		}
	}
	
	qsort(NID, node_num, sizeof(int), cmp_by_node_id);

	printf("current nodes list: ");
	for(int k=0; k<node_num; k++)
	{
		printf("%d ", NID[k]);
	}
	printf("\n");
	for(int i=0;i<ch->points_per_bucket;i++)
    {
        for(int j=0;j<ch->bucket_id_upbound;j++)
        {
		
			randval=(((uint32_t)(-1))/(ch->points_per_bucket*(NID[j]+i+1)))*((i+1)*(NID[j]+i)*ch->points_per_bucket+SWARMKV_DEFAULT_SLOT_NUM);
            ch->point[i*ch->bucket_id_upbound+j].node_id=NID[j];
            ch->point[i*ch->bucket_id_upbound+j].point_val=randval % SWARMKV_DEFAULT_SLOT_NUM;
			ch->point[i*ch->bucket_id_upbound+j].hit_cnt=0;
		}
	}
	qsort(ch->point,ch->point_num, sizeof(struct ch_point),cmp_by_key);
	return ch;
}
*/
	/*
			int t = NID[j]*171+171;
			srand((unsigned)t);
			uint32_t tmp_rand = rand();
			randval = tmp_rand % SWARMKV_DEFAULT_SLOT_NUM + SWARMKV_DEFAULT_SLOT_NUM/(i+1);
			printf("%d %d %u %u\n", t, NID[j], randval, tmp_rand);
			*/

int conhash_add_point( struct conhash *ch, uint32_t node_id )
{
	struct ch_point* point_list=calloc(sizeof(struct ch_point),ch->point_num+1);
	memcpy(point_list,ch->point,sizeof(struct ch_point)*ch->point_num);
	point_list[ch->point_num].node_id = node_id;
	point_list[ch->point_num].point_val=rand();
	qsort(point_list,ch->point_num+1,sizeof(struct ch_point),cmp_by_key);
	free(ch->point);
	ch->point=point_list;
	ch->point_num++;
	return 0;
}



void conhash_destroy( struct conhash *ch )
{
	free(ch->point);
	ch->point=NULL;
	free(ch->bucket_hit);
	ch->bucket_hit=NULL;
	free(ch);
}

