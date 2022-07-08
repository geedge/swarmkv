#include "or_set.h"
#include "uthash.h"
#include "mpack.h"
#include <assert.h>
#include <string.h>

#ifndef MAX
#define MAX(a, b)  	(((a) > (b)) ? (a) : (b))
#endif

#ifndef ALLOC
#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#endif

struct OR_timestamp
{
	char* replica_id;
	size_t replica_id_sz;	
	unsigned long long sequence;
	UT_hash_handle hh;
};
struct OR_timestamp * OR_timestamp_new(const char* replica_id, size_t replica_id_sz, unsigned long long sequence)
{
	struct OR_timestamp *ts=ALLOC(struct OR_timestamp, 1);
	ts->replica_id_sz=replica_id_sz;
	ts->replica_id=ALLOC(char, ts->replica_id_sz);
	memcpy(ts->replica_id, replica_id, ts->replica_id_sz);
	ts->sequence=sequence;
	return ts;
}
void OR_timestamp_free(struct OR_timestamp *ts)
{
	free(ts->replica_id);
	free(ts);
	return;
}
struct OR_member
{
	char *buffer;
	size_t buffer_size;
	struct OR_timestamp *hash_timestamp;
	int should_merge_flag;
	UT_hash_handle hh;
};
struct OR_member * OR_member_new(const char *member, size_t member_sz)
{
	struct OR_member *mb=NULL;
	mb=ALLOC(struct OR_member, 1);
	mb->buffer=ALLOC(char, member_sz);
	mb->buffer_size=member_sz;		
	memcpy(mb->buffer, member, member_sz);		
	return mb;
}
void OR_member_free(struct OR_member *mb)
{
	struct OR_timestamp *ts=NULL, *tmp=NULL;
	HASH_ITER(hh, mb->hash_timestamp, ts, tmp)
	{
		HASH_DEL(mb->hash_timestamp, ts);
		OR_timestamp_free(ts);
	}
	free(mb->buffer);
	free(mb);
}
struct OR_set
{
	char* my_id;
	size_t id_sz;
	unsigned long long sequence_generator;
	struct OR_member *hash_members;
	struct OR_timestamp *hash_replica_vector;
};
struct OR_set *OR_set_new(const char* id, size_t id_sz)
{
	struct OR_set *set=ALLOC(struct OR_set, 1);
	set->id_sz=id_sz;
	set->my_id=ALLOC(char, id_sz);
	memcpy(set->my_id, id, set->id_sz);
	return set;
}
void OR_set_free(struct OR_set *set)
{
	struct OR_member *mb=NULL, *tmp_mb=NULL;
	HASH_ITER(hh, set->hash_members, mb, tmp_mb)
	{
		HASH_DEL(set->hash_members, mb);
		OR_member_free(mb);
	}
	struct OR_timestamp *ts=NULL, *tmp_ts=NULL;
	HASH_ITER(hh, set->hash_replica_vector, ts, tmp_ts)
	{
		HASH_DEL(set->hash_replica_vector, ts);
		OR_timestamp_free(ts);
	}
	free(set->my_id);
	free(set);
	return;
}
int OR_set_add(struct OR_set *set, const char *member, size_t member_sz)
{
	struct OR_member *mb=NULL;
	struct OR_timestamp *ts=NULL;
	int ret=0;
	HASH_FIND(hh, set->hash_members, member, member_sz, mb);
	if(!mb)
	{		
		mb=OR_member_new(member, member_sz);
		HASH_ADD_KEYPTR(hh, set->hash_members, mb->buffer, mb->buffer_size, mb);
		ret=1;
	}
	unsigned long long new_sequence=set->sequence_generator++;
	HASH_FIND(hh, mb->hash_timestamp, set->my_id, set->id_sz, ts);
	if(!ts)
	{
		ts=OR_timestamp_new(set->my_id, set->id_sz, new_sequence);
		HASH_ADD_KEYPTR(hh, mb->hash_timestamp, ts->replica_id, ts->replica_id_sz, ts);
	}
	else
	{
		ts->sequence=new_sequence;
	}
	
	HASH_FIND(hh, set->hash_replica_vector, set->my_id, set->id_sz, ts);
	if(!ts)
	{
		ts=OR_timestamp_new(set->my_id, set->id_sz, new_sequence);	
		HASH_ADD_KEYPTR(hh, set->hash_replica_vector, ts->replica_id, ts->replica_id_sz, ts);
	}
	else
	{
		ts->sequence=new_sequence;
	}
	return ret;
}
int OR_set_remove(struct OR_set *set, const char *member, size_t member_sz)
{
	struct OR_member *mb=NULL;	
	HASH_FIND(hh, set->hash_members, member, member_sz, mb);
	if(!mb)
	{
		return 0;
	}
	HASH_DELETE(hh, set->hash_members, mb);
	OR_member_free(mb);
	return 1;
}
int OR_set_is_member(struct OR_set *set, const char* member, size_t member_sz)
{	
	struct OR_member *mb=NULL;
	HASH_FIND(hh, set->hash_members, member, member_sz, mb);
	if(mb)
	{
		return 1;
	}
	return 0;
}

void OR_set_member_list_free(struct OR_set_member_list *list)
{
	size_t i=0;
	for(i=0; i<list->n_member; i++)
	{
		free(list->members[i]);
		list->members[i]=NULL;
	}
	free(list->members);
	free(list->member_sz);
	free(list);
	return;
}
struct OR_set_member_list* OR_set_members(struct OR_set *set)
{
	size_t n_member=HASH_CNT(hh, set->hash_members);
	if(n_member==0)
	{
		return NULL;
	}
	struct OR_set_member_list *list=NULL;	
	list=ALLOC(struct OR_set_member_list, 1);
	list->n_member=n_member;
	list->members=ALLOC(char*, n_member);
	list->member_sz=ALLOC(size_t, n_member);
	
	
	struct OR_member *mb=NULL, *tmp=NULL;
	size_t i=0;
	HASH_ITER(hh, set->hash_members, mb, tmp)
	{
		list->member_sz[i]=mb->buffer_size;
		list->members[i]=ALLOC(char, list->member_sz[i]);
		memcpy(list->members[i], mb->buffer, list->member_sz[i]);
		i++;
	}
	return list;
}
static void OR_member_merge(struct OR_member *dst, const struct OR_member *src)
{
	struct OR_timestamp *src_ts=NULL, *dst_ts=NULL, *tmp=NULL;
	HASH_ITER(hh, src->hash_timestamp, src_ts, tmp)
	{
		HASH_FIND(hh, dst->hash_timestamp, src_ts->replica_id, src_ts->replica_id_sz, dst_ts);
		if(dst_ts)
		{
			dst_ts->sequence=MAX(dst_ts->sequence, src_ts->sequence);
		}
		else
		{
			dst_ts=OR_timestamp_new(src_ts->replica_id, src_ts->replica_id_sz, src_ts->sequence);
			HASH_ADD_KEYPTR(hh, dst->hash_timestamp, dst_ts->replica_id, dst_ts->replica_id_sz, dst_ts);
		}
	}
	return;
}

static int is_removed_by_peer(struct OR_member *mb, const struct OR_timestamp *hash_peer_vector)
{
	struct OR_timestamp *ts=NULL, *tmp=NULL, *remote_ts=NULL;
	HASH_ITER(hh, mb->hash_timestamp, ts, tmp)
	{
		HASH_FIND(hh, hash_peer_vector, ts->replica_id, ts->replica_id_sz, remote_ts);
		if(!remote_ts || ts->sequence>remote_ts->sequence)
		{
			continue;
		}
		HASH_DEL(mb->hash_timestamp, ts);
		OR_timestamp_free(ts);
	}
	return mb->hash_timestamp?1:0;
}
size_t OR_set_cardinality(struct OR_set *set)
{
	return HASH_COUNT(set->hash_members);
}
size_t OR_set_replica_number(struct OR_set *set)
{
	return HASH_COUNT(set->hash_replica_vector);
}
void OR_set_serialize(struct OR_set *set, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct OR_member *mb=NULL, *tmp_mb=NULL;
	struct OR_timestamp *ts=NULL, *tmp_ts=NULL;
	
	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);	
	mpack_build_map(&writer);
	
	mpack_write_cstr(&writer, "a");

	mpack_start_array(&writer, HASH_COUNT(set->hash_members));//Start member list
	HASH_ITER(hh, set->hash_members, mb, tmp_mb)
	{
		mpack_start_map(&writer, 2);//Start a member
		mpack_write_cstr(&writer, "mb");
		mpack_write_str(&writer, mb->buffer, mb->buffer_size);
		
		mpack_write_cstr(&writer, "ts");
		mpack_start_array(&writer, HASH_CNT(hh, mb->hash_timestamp)*2);//Start timestamps
		HASH_ITER(hh, mb->hash_timestamp, ts, tmp_ts)
		{
			mpack_write_str(&writer, ts->replica_id, ts->replica_id_sz);
			mpack_write_u64(&writer, ts->sequence);
		}
		mpack_finish_array(&writer);//End timestamps
		
		mpack_finish_map(&writer); //End a member
	}
	mpack_finish_array(&writer);//End member list

	mpack_write_cstr(&writer, "v");
	mpack_start_array(&writer, HASH_COUNT(set->hash_replica_vector)*2);//Start replica vector
	HASH_ITER(hh, set->hash_replica_vector, ts, tmp_ts)
	{
		mpack_write_str(&writer, ts->replica_id, ts->replica_id_sz);
		mpack_write_i64(&writer, ts->sequence);
	}
	mpack_finish_array(&writer); //End replica vector

	mpack_complete_map(&writer);
	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the OR-Set!\n");
		return;
	}
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
}
static struct OR_set *OR_set_deserialize(const char *blob, size_t blob_sz)
{
	struct OR_set *set=ALLOC(struct OR_set, 1);

	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	struct OR_member *mb=NULL;
	struct OR_timestamp *ts=NULL;
	mpack_node_t mpnode_member_array=mpack_node_map_cstr(root, "a");
	size_t n_member=0, n_ts=0;
	n_member=mpack_node_array_length(mpnode_member_array);
	mpack_node_t mpnode_member, mpnode_buffer, mpnode_ts_array, mpnode_ts_replica, mpnode_ts_seq;
	

	for(size_t i=0; i<n_member; i++)
	{
		mpnode_member=mpack_node_array_at(mpnode_member_array, i);
		mpnode_buffer=mpack_node_map_cstr(mpnode_member, "mb");
		mb=OR_member_new(mpack_node_str(mpnode_buffer), mpack_node_strlen(mpnode_buffer));
		
		mpnode_ts_array=mpack_node_map_cstr(mpnode_member, "ts");
		n_ts=mpack_node_array_length(mpnode_ts_array);
		
		for(size_t j=0; j<n_ts; j+=2)
		{
			mpnode_ts_replica=mpack_node_array_at(mpnode_ts_array, j);
			mpnode_ts_seq=mpack_node_array_at(mpnode_ts_array, j+1);
			ts=OR_timestamp_new(mpack_node_str(mpnode_ts_replica), mpack_node_strlen(mpnode_ts_replica), mpack_node_u64(mpnode_ts_seq));
			HASH_ADD_KEYPTR(hh, mb->hash_timestamp, ts->replica_id, ts->replica_id_sz, ts);
		}
		
		HASH_ADD_KEYPTR(hh, set->hash_members, mb->buffer, mb->buffer_size, mb);		
	}

	mpnode_ts_array=mpack_node_map_cstr(root, "v");
	n_ts=mpack_node_array_length(mpnode_ts_array);
	for(size_t i=0; i<n_ts; i+=2)
	{
		mpnode_ts_replica=mpack_node_array_at(mpnode_ts_array, i);
		mpnode_ts_seq=mpack_node_array_at(mpnode_ts_array, i+1);
		ts=OR_timestamp_new(mpack_node_str(mpnode_ts_replica), mpack_node_strlen(mpnode_ts_replica), mpack_node_u64(mpnode_ts_seq));
		HASH_ADD_KEYPTR(hh, set->hash_replica_vector, ts->replica_id, ts->replica_id_sz, ts);
	}

	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred decoding the OR_set!\n");
	}
	return set;
}
void OR_set_merge(struct OR_set *set, const char *blob, size_t blob_sz)
{
	struct OR_set *to_merge=NULL;
	struct OR_member *mb=NULL, *mb_to_merge=NULL, *tmp=NULL, *tmp_to_merge=NULL;
	to_merge=OR_set_deserialize(blob, blob_sz);
	HASH_ITER(hh, set->hash_members, mb, tmp)
	{
		HASH_FIND(hh, to_merge->hash_members, mb->buffer, mb->buffer_size, mb_to_merge);
		if(mb_to_merge)
		{
			OR_member_merge(mb, mb_to_merge);
			mb->should_merge_flag=1;
		}
		else
		{
			mb->should_merge_flag=is_removed_by_peer(mb, to_merge->hash_replica_vector);
		}
	}
	HASH_ITER(hh, to_merge->hash_members, mb_to_merge, tmp_to_merge)
	{
		HASH_FIND(hh, set->hash_members, mb_to_merge->buffer, mb_to_merge->buffer_size, mb);
		if(mb)
		{
			assert(mb->should_merge_flag==1);
			continue;
		}
		mb_to_merge->should_merge_flag=is_removed_by_peer(mb_to_merge, set->hash_replica_vector);
		if(mb_to_merge->should_merge_flag)
		{
			HASH_DEL(to_merge->hash_members, mb_to_merge);
			HASH_ADD_KEYPTR(hh, set->hash_members, mb_to_merge->buffer, mb_to_merge->buffer_size, mb_to_merge);
		}
	}
	HASH_ITER(hh, set->hash_members, mb, tmp)
	{
		if(!mb->should_merge_flag)
		{
			HASH_DEL(set->hash_members, mb);
			OR_member_free(mb);
		}
		else
		{
			mb->should_merge_flag=0;//reset flag
		}
	}
	struct OR_timestamp *ts=NULL, *to_merge_ts=NULL, *tmp_ts=NULL;
	HASH_ITER(hh, to_merge->hash_replica_vector, to_merge_ts, tmp_ts)
	{
		HASH_FIND(hh, set->hash_replica_vector, to_merge_ts->replica_id, to_merge_ts->replica_id_sz, ts);
		if(ts)
		{
			ts->sequence=MAX(ts->sequence, to_merge_ts->sequence);
		}
		else
		{
			HASH_DEL(to_merge->hash_replica_vector, to_merge_ts);
			HASH_ADD_KEYPTR(hh, set->hash_replica_vector, to_merge_ts->replica_id, to_merge_ts->replica_id_sz, to_merge_ts);
		}
	}
	OR_set_free(to_merge);
	return;
}
