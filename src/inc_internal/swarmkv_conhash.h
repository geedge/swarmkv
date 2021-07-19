#ifndef __CONSISTENT_HASH_H__
#define __CONSISTENT_HASH_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "swarmkv_internal.h"

struct ch_point
{
	uint32_t	node_id;      //node_id, one node may have multiple birtual point
	uint32_t	hit_cnt;
	uint32_t	point_val;      //hash value in consistent hash ring
};

struct conhash
{
	uint32_t	bucket_id_upbound;
	uint32_t	bucket_cnt;
	uint32_t	points_per_bucket;
	double		compress_ratio;
	uint32_t	point_num;
	uint32_t	*bucket_hit;
	struct ch_point *point;
};

void *swarmkv_conhash_create(struct swarmkv_node *nodes, uint32_t points_per_bucket);

#ifdef __cplusplus
}
#endif
#endif
