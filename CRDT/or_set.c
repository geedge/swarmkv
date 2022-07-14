#include "or_map.h"
#include "or_set.h"
#include "crdt_utils.h"

#include <assert.h>
#include <string.h>

struct OR_set
{
	struct OR_map *map;
};
struct OR_set *OR_set_new(const char* id, size_t id_sz)
{
	struct OR_set *set=ALLOC(struct OR_set, 1);
	set->map=OR_map_new(id, id_sz);
	return set;
}
void OR_set_free(struct OR_set *set)
{
	OR_map_free(set->map);
	free(set);
	return;
}
int OR_set_add(struct OR_set *set, const char *member, size_t member_len)
{
	int ret=0;
	ret=OR_map_add(set->map, member, member_len);
	return ret;
}
int OR_set_remove(struct OR_set *set, const char *member, size_t member_len)
{
	return OR_map_remove(set->map, member, member_len);
}
int OR_set_is_member(struct OR_set *set, const char* member, size_t member_len)
{
	return OR_map_exisits(set->map, member, member_len);
}
size_t OR_set_cardinality(struct OR_set *set)
{
	return OR_map_cardinality(set->map);
}
size_t OR_set_replica_number(struct OR_set *set)
{
	return OR_map_replica_number(set->map);
}

struct OR_set_member_list_container
{
	struct OR_set_member_list mlist;
	struct OR_map_kv **kv_list;
};
void OR_set_member_list_free(struct OR_set_member_list *list)
{
	struct OR_set_member_list_container *list_container=container_of(list, struct OR_set_member_list_container, mlist);
	
	free(list->members);
	free(list->member_len);
	for(size_t i=0; i<list->n_member; i++)
	{
		OR_map_kv_free(list_container->kv_list[i]);
	}	
	free(list_container->kv_list);
	list->n_member=0;
	free(list_container);
	return;
}
struct OR_set_member_list* OR_set_members(struct OR_set *set)
{
	struct OR_map_kv **kv_list=NULL;
	size_t n_kv=0;
	n_kv=OR_map_keys(set->map, &kv_list);
	if(n_kv==0)
	{
		return NULL;
	}
	struct OR_set_member_list_container *list_container=ALLOC(struct OR_set_member_list_container, 1);	
	struct OR_set_member_list *mlist=&(list_container->mlist);
	list_container->kv_list=kv_list;

	mlist->n_member=n_kv;
	mlist->members=ALLOC(char*, n_kv);
	mlist->member_len=ALLOC(size_t, n_kv);
	for(size_t i=0; i<n_kv; i++)
	{
		mlist->members[i]=kv_list[i]->key;
		mlist->member_len[i]=kv_list[i]->key_len;
	}
	return mlist;
}

void OR_set_serialize(struct OR_set *set, char **blob, size_t *blob_sz)
{
	OR_map_serialize(set->map, blob, blob_sz);
	return;
}
void OR_set_merge_blob(struct OR_set *set, const char *blob, size_t blob_sz)
{
	OR_map_merge_blob(set->map, blob, blob_sz);
	return;
}

