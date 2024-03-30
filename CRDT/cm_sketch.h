/*
* A CRDT Count-Min Sketch implementation in C.
* Cormode, Graham; S. Muthukrishnan (2005). "An Improved Data Stream Summary: The Count-Min Sketch and its Applications"
* Author: contact@zhengchao.io
* Count-Min Sketch is a probabilistic data-structure that takes sub linear space to store the probable count, 
* or frequency, of occurrences of elements added into the data-structure.
* Most of the code is from https://github.com/barrust/count-min-sketch.
*/

#pragma once
#include <uuid/uuid.h>
#ifdef __cplusplus
extern "C"
{
#endif
struct CM_sketch;
struct CM_sketch *CM_sketch_new(uuid_t my_id, int width, int depth);
void CM_sketch_free(struct CM_sketch *cms);
int CM_sketch_query(const struct CM_sketch *cms, const char *item, size_t len);
/*
 * @param increment: the increment value, can be negative.
*/
int CM_sketch_incrby(struct CM_sketch *cms, const char *item, size_t len, int increment);

size_t CM_sketch_size(const struct CM_sketch *cms);
void CM_sketch_serialize(const struct CM_sketch *cms, char **blob, size_t *blob_sz);
struct CM_sketch *CM_sketch_deserialize(const char *blob, size_t blob_sz);
void CM_sketch_merge_blob(struct CM_sketch *dst, const char *blob, size_t blob_sz);
struct CM_sketch *CM_sketch_replicate(uuid_t uuid, const char *blob, size_t blob_sz);
struct CM_sketch_info
{
    int width;
    int depth;
    double error;
    double probability;
    long long count;
    long long n_replica;
};
void CM_sketch_info(const struct CM_sketch *cms, struct CM_sketch_info *info);
/*  Recommends width & depth for expected n different items,
    with probability of an error  - prob and over estimation
    error - overEst (use 1 for max accuracy) */
/*
 * @param error: Estimate size of error. The error is a percent of total counted items. This effects the width of the sketch.
 * @param probability: The desired probability for inflated count. This should be a decimal value between 0 and 1. This effects the depth of the sketch. For example, for a desired false positive rate of 0.1% (1 in 1000), error_rate should be set to 0.001. The closer this number is to zero, the greater the memory consumption per item and the more CPU usage per operation.
*/
void CM_sketch_tool_dimension_by_prob(double error, double probability, int *width, int *depth);
/**/
void CM_sketch_list_replica(const struct CM_sketch *cms, uuid_t *nodes, size_t *n_nodes);
int CM_sketch_clear_replica(struct CM_sketch *cms, uuid_t node_uuid);
#ifdef __cplusplus
}
#endif