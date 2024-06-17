/*
Refer to
A High-Performance Invertible Sketch for Network-Wide Superspreader Detection
*/


#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
#include <sys/time.h>

struct spread_sketch;
struct ss_query_result {
    char **key;
    size_t *key_len;
    double *count;
    int n_results;
};

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now);
void spread_sketch_update(struct spread_sketch *ss, const char *key_main, size_t key_main_length, const char *key_spread, size_t key_spread_length, struct timeval now);

struct ss_query_result *spread_sketch_query(const struct spread_sketch *ss, struct timeval now);
void ss_query_result_free(struct ss_query_result *result);
void spread_sketch_free(struct spread_sketch *ss);

void spread_sketch_merge(struct spread_sketch *dst, const struct spread_sketch *src);
void spread_sketch_serialize(const struct spread_sketch *ss, char **blob, size_t *blob_sz);
struct spread_sketch *spread_sketch_deserialize(const char *blob, size_t blob_sz);

size_t spread_sketch_calculate_memory_usage(const struct spread_sketch *ss);
void get_parameter_recommendation(int max_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out);

#ifdef __cplusplus
}
#endif