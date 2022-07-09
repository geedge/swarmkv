/*
* Add-Wins Observed-Remove Set that is state-based and has no tombstones. 
* A C language implementation of Bieniusa, Annette, et al. "An optimized conflict-free replicated set." arXiv preprint arXiv:1210.3368 (2012).
* Author: contact@zhengchao.io
* Space complexity: The payload size of OptORSet set is bounded by O(|elements|n+n) at any moment, where n is the number
* of processes in the systems and |elements| is the number of elements present in the set. 
* The first component corresponds to the maximum number of timestamps in set E and the second captures the size of the vector v. 
* In the common case, where the number of processes repeatedly invoking adds can be considered a constant, the payload size
* is O(|elements| + n).
*/

#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct OR_set;
struct OR_set *OR_set_new(const char* id, size_t id_sz);
void OR_set_free(struct OR_set *set);
int OR_set_add(struct OR_set *set, const char *member, size_t member_sz);
int OR_set_remove(struct OR_set *set, const char *member, size_t member_sz);
int OR_set_is_member(struct OR_set *set, const char* member, size_t member_sz);
size_t OR_set_cardinality(struct OR_set *set);
size_t OR_set_replica_number(struct OR_set *set);

struct OR_set_member_list
{
	size_t n_member;
	char **members;
	size_t *member_sz;
};
void OR_set_member_list_free(struct OR_set_member_list *list);
struct OR_set_member_list* OR_set_members(struct OR_set *set);

void OR_set_serialize(struct OR_set *set, char **blob, size_t *blob_sz);
void OR_set_merge(struct OR_set *set, const char *blob, size_t blob_sz);


#ifdef __cplusplus
}
#endif

