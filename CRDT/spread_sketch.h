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

#define DECAY_L_FLAG true

struct spread_sketch;
struct ss_query_result {
    char **key;
    double *count;
    int n_results;
};

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now);
void spread_sketch_update(struct spread_sketch *ss, const char *key_main, const char *key_spread, struct timeval now);

struct ss_query_result *spread_sketch_query(const struct spread_sketch *ss, struct timeval now);
void ss_query_result_free(struct ss_query_result *result);
void spread_sketch_free(struct spread_sketch *ss);

double spread_sketch_point_query(struct spread_sketch *ss, const char *key_main);
size_t spread_sketch_calculate_memory_usage(const struct spread_sketch *ss);

#ifdef __cplusplus
}
#endif