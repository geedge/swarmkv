#pragma once



#ifdef __cplusplus
extern "C"{
#endif

#include "exdata.h"
#include <stdint.h> //uint64_t
#include <sys/time.h> // struct timeval
#include <stddef.h>
#include <uuid/uuid.h>

#define DUMMY_ITEM_HASH (1ULL<<63) // level(left most zeros) = 0

struct spread_sketch;

// set time_window_ms to zero, the spread sketch will not slide and decay, in which case, `now` can be any value
struct spread_sketch *spread_sketch_new(int depth, int width, unsigned char precision, int time_window_ms, struct timeval now);
void spread_sketch_free(struct spread_sketch *ss);
void spread_sketch_set_exdata_schema(struct spread_sketch *ss, exdata_new_cb new_fn, exdata_free_cb free_fn, exdata_merge_cb merge_fn, exdata_reset_cb reset_fn, exdata_copy_cb copy_fn);

int spread_sketch_add_hash(struct spread_sketch *ss, const char *entry, size_t entry_length, uint64_t item_hash, void *arg, struct timeval now);
int spread_sketch_add(struct spread_sketch *ss, const char *entry, size_t entry_length, const char* item, size_t item_len, void *arg, struct timeval now);

// get the number of entrys stored in spread sketch
int spread_sketch_get_count(const struct spread_sketch *ss);
// list all the entrys in spread sketch. User should free the arrays, but do not free the elements of strings in the array(because they are references to the internal data structure)
// Example: char **entry; size_t *entry_len; size_t n_entrys; spread_sketch_list_entrys(&entry, &entry_len, &n_entrys); free(entry); free(entry_len);
void spread_sketch_list_entries(const struct spread_sketch *ss, char ***entries, size_t **entry_lens, size_t *n_entry);
// query the cardinality(or fanout) of a entry in spread sketch. 
// Even thought spread sketch algorithm does not requires entrys to exist innately, when querying a entry that is not present in the spread sketch, `spread_sketch_get_cardinality` will return -1.
double spread_sketch_get_cardinality(const struct spread_sketch *ss, const char *entry, size_t entry_len);
// query a hyperloglog 's base64 serialization. The serialization format is [1,precision,register...] and then encoded by base64
char *spread_sketch_get_hll_base64_serialization(const struct spread_sketch *ss, const char *entry, size_t entry_len);
void *spread_sketch_get0_exdata(const struct spread_sketch *ss, const char *entry, size_t entry_len);
// in most cases, it has the same output as `spread_sketch_get_cardinality`, but it will perform more like an ordinary spread sketch query. 
// Will always return a value, even if the entry is not present in the spread sketch. Must pass a `now` value required by Stagger hll query.
double spread_sketch_query(const struct spread_sketch *ss, const char *entry, size_t entry_length, struct timeval now);

void spread_sketch_merge(struct spread_sketch *dest, const struct spread_sketch *src);
struct spread_sketch *spread_sketch_copy(const struct spread_sketch *src);
void spread_sketch_serialize(const struct spread_sketch *ss, char **blob, size_t *blob_sz);
struct spread_sketch *spread_sketch_deserialize(const char *blob, size_t blob_sz);
void spread_sketch_merge_blob(struct spread_sketch *dst, const char *blob, size_t blob_sz);
void spread_sketch_reset(struct spread_sketch *ss);
// exactly the same as `spread_sketch_deserialize`. `uuid` is a dummy parameter.(since spread sketch itself performs very like set CRDT, it does not need uuid to sync replicas)
struct spread_sketch *spread_sketch_replicate(uuid_t uuid, const char *blob, size_t blob_sz);

void spread_sketch_get_parameter(const struct spread_sketch *ss, int *depth_out, int *width_out, unsigned char *precision_out, int *time_window_ms_out);
// spread sketch alway store values more than expected_query_num,expected_query_num is a hint to set spread sketch parameters properly
void spread_sketch_recommend_parameters(int expected_super_spreader_number, int *depth_out, int *width_out, unsigned char *precision_out);
size_t spread_sketch_calculate_memory_usage(const struct spread_sketch *ss);

#ifdef __cplusplus
}
#endif