#pragma once

#include <stddef.h>


#ifdef __cplusplus
extern "C"{
#endif

/* ---------------------------------------------------------------------------------------
 A sorted set realization using min heap + uthash. Has limited size. Find: O(1), Add: O(logn), Pop: O(logn)
 Not a CRDT structure, currently only used by heavy keeper.
 Auther: Chen Zizhan 
 Time: 2023/5/22
*/

struct sorted_set;

/**
 * @brief Create a new sorted set.
 * @param set_size the max size of the sorted set.
 * @return the pointer to the sorted set.
*/
struct sorted_set *sorted_set_new(int set_size);

/**
 * @brief Destroy the sorted set.
 * @param ss the pointer to the sorted set
*/
void sorted_set_free(struct sorted_set *ss);

/** 
 * @brief insert a new entry into the sorted set. The minimum entry will be popped if the list is full.
 * @param ss the sorted set
 * @param member the key of the entry, a string of flow id.
 * @param member_size the length of the key, not including the '\0' terminator.
 * @param cnt The initial score for the member inserted.
*/
void sorted_set_insert(struct sorted_set *ss, const char *member, size_t member_size, unsigned cnt);

/**
 * @brief pop out the minumum-score entry from the sorted set.
 * @param ss the sorted set
 * @return the score of the minimum-score member. return 0 if the sorted set is empty.
*/
unsigned sorted_set_pop(struct sorted_set *ss);

/**
 * @brief Increase the score of the given member.
 * @param ss the sorted set
 * @param member the key of the entry, a string of flow id.
 * @param member_size the length of the key, not including the '\0' terminator.
 * @param score By the value of which increase member.
*/
void sorted_set_incrby(struct sorted_set *ss, const char *member, size_t member_size, unsigned score);

/**
 * @brief get the minimum score from the sorted set.
 * @param ss the sorted set
 * @return the score of the minimum-score member
*/
unsigned sorted_set_get_min_score(const struct sorted_set *ss);

/** 
 * @brief find the how many elements the sorted set contains.
*/
int sorted_set_cardinality(const struct sorted_set *ss);

/**
 * @brief Get the score of the given key string.
 * @param ss the sorted set
 * @param member the key of the entry, a string of flow id.
 * @param member_size the length of the key, not including the '\0' terminator.
 * @return the score of the member. return 0 if the member is not in the sorted set.
*/
unsigned sorted_set_get_member_score(const struct sorted_set *ss, const char *member, size_t member_size);

/**
 * @brief Get all the contents in sorted order from small to big.
 * @param ss the sorted set
 * @param scores_out the scores of the entries. User should alloc it and make sure the buffer is long enough.
 * @param members_out the member name strings of the entries. User should alloc it and make sure the buffer is long enough. Its content is the pointer to the member name string in the sorted set. Don't free the content.
 * @param members_len_out the length of the member name strings. User should alloc it and make sure the buffer is long enough. Including the '\0' terminator.
*/
void sorted_set_dump(const struct sorted_set *ss, unsigned *scores_out, char **members_out, size_t *members_len_out);

/*
    * @brief Get the total memory usage of the sorted set.
    * @param ss the sorted set
    * @return the total memory usage of the sorted set.
*/
size_t sorted_set_cal_total_memory_usage(const struct sorted_set *ss);

#ifdef __cplusplus 
}//end extern "C"
#endif

