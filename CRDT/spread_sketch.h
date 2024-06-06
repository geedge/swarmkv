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

#define DECAY_L_FLAG false

struct spread_sketch;
struct ss_query_result {
    char *key;
    double count;
};

struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_s, struct timeval now);
void spread_sketch_update(struct spread_sketch *ss, const char *key_main, const char *key_spread, struct timeval now);

void spread_sketch_query(struct spread_sketch *ss, struct timeval now, struct ss_query_result **results, int *n_results);
void spread_sketch_free(struct spread_sketch *ss);

double spread_sketch_point_query(struct spread_sketch *ss, const char *key_main);


#ifdef __cplusplus
}
#endif