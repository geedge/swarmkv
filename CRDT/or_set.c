struct OR_id
{
	uuid_t node_uuid;
	long long sequence;	
	UT_hash_handle hh;
};

struct OR_member
{
	char *buffer;
	size_t buffer_size;
	struct OR_id *hash_ids;
	UT_hash_handle hh;
};

struct OR_set
{
	uuid_t my_uuid;
	long long sequence_generator;
	struct OR_member *hash_members;
	struct OR_id *hash_removed_ids;
};
struct OR_set *OR_set_new(uuid_t my_id)
{
	struct OR_set *set=ALLOC(struct OR_set, 1);
	
}
void OR_set_add(struct OR_set *set, const char *member, size_t member_sz)
{
	struct OR_member *mb=NULL;	
	struct OR_id *id=NULL;
	HASH_FIND(hh, set->hash_members, member, member_sz, mb);
	if(!mb)
	{
		mb=ALLOC(struct OR_member, 1);
		mb->buffer=ALLOC(char, member_sz);
		mb->buffer_size=member_sz;		
		memcpy(mb->buffer, member, member_sz);		
		HASH_ADD(hh, set->hash_members, member, member_sz, mb);
	}
	id=ALLOC(struct OR_id, 1);
	uuid_copy(id->node_uuid, set->my_uuid);
	id->sequence=set->sequence_generator++;
	HASH_ADD(hh, mb->hash_ids, node_uuid, sizeof(struct OR_id) - sizeof(UT_hash_handle), id);
	return;
}
static int OR_set_is_member_removed(struct OR_set *set, struct OR_member *mb)
{
	struct OR_id *mb_id, *tmp=NULL, *rm_id=NULL;
	HASH_ITER(hh, mb->hash_ids, mb_id, tmp)
	{
		HASH_FIND(hh, set->hash_removed_ids, mb_id->node_uuid, sizeof(struct OR_id)-sizeof(UT_hash_handle), rm_id);
		if(!rm_id)
		{
			return 0;
		}
	}
	return 1;
}
int OR_set_is_member(struct OR_set *set, const char* member, size_t member_sz)
{	
	struct OR_member *mb=NULL;
	struct OR_id *mb_id, *tmp=NULL, *rm_id=NULL;
	HASH_FIND(hh, set->hash_members, member, member_sz, mb);
	if(mb && !OR_set_is_member_removed(set, mb))
	{
		return 1;
	}
	return 0;
}
void OR_set_members(struct OR_set *set, sds **out_members, size_t *n_out_member)
{
	struct OR_member *mb=NULL, *tmp=NULL;

	HASH_ITER(hh, set->hash_members, mb, tmp)
	{
		if(!OR_set_is_member_removed(set, mb))
		{

		}
	}
}
