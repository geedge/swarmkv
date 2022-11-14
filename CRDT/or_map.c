#include "lww_register.h"
#include "pn_counter.h"
#include "or_map.h"
#include "crdt_utils.h"
#include "uthash.h"
#include "mpack.h"
#include <assert.h>
#include <string.h>


struct OR_timestamp
{
	uuid_t uuid;
	unsigned long long sequence;
	UT_hash_handle hh;
};
struct OR_timestamp * OR_timestamp_new(uuid_t uuid, unsigned long long sequence)
{
	struct OR_timestamp *ts=ALLOC(struct OR_timestamp, 1);
	uuid_copy(ts->uuid, uuid);
	ts->sequence=sequence;
	return ts;
}
void OR_timestamp_free(struct OR_timestamp *ts)
{
	free(ts);
	return;
}
struct OR_record
{
	char *key;
	size_t key_len;
	uuid_t replica_id;
	enum value_type type;
	union
	{	
		struct LWW_register *string;
		struct PN_counter *counter;
		void *none;
	};
	struct OR_timestamp *hash_timestamp;
	int should_merge_flag;
	UT_hash_handle hh;
};
struct OR_record * OR_record_new(uuid_t replica_id, const char *member, size_t member_sz, enum value_type type)
{
	struct OR_record *record=NULL;
	record=ALLOC(struct OR_record, 1);
	record->key=ALLOC(char, member_sz);
	record->key_len=member_sz;		
	memcpy(record->key, member, member_sz);
	uuid_copy(record->replica_id, replica_id);
	record->type=type;
	switch(record->type)
	{
		case TYPE_STRING:
			record->string=LWW_register_new(replica_id);
			break;
		case TYPE_INTEGER:
			record->counter=PN_counter_new(replica_id);
			break;
		default:
			record->none=NULL;
			break;
	}
	return record;
}
void OR_record_free(struct OR_record *record)
{
	struct OR_timestamp *ts=NULL, *tmp=NULL;
	HASH_ITER(hh, record->hash_timestamp, ts, tmp)
	{
		HASH_DEL(record->hash_timestamp, ts);
		OR_timestamp_free(ts);
	}
	switch(record->type)
	{
		case TYPE_STRING:
			LWW_register_free(record->string);
			record->string=NULL;
			break;
		case TYPE_INTEGER:
			PN_counter_free(record->counter);
			record->counter=NULL;
			break;
		default:
			break;
	}
	free(record->key);
	free(record);
	return;
}
void OR_record_step(struct OR_record *record, uuid_t my_uuid, unsigned long long sequence)
{
	struct OR_timestamp *ts=NULL;
	HASH_FIND(hh, record->hash_timestamp, my_uuid, sizeof(uuid_t), ts);
	if(!ts)
	{
		ts=OR_timestamp_new(my_uuid, sequence);
		HASH_ADD_KEYPTR(hh, record->hash_timestamp, ts->uuid, sizeof(ts->uuid), ts);
	}
	else
	{
		ts->sequence=sequence;
	}
	return;
}
void OR_record_set_str(struct OR_record *record, const char *value, size_t value_sz)
{
	if(record->type==TYPE_NONE)
	{
		record->string=LWW_register_new(record->replica_id);
		record->type=TYPE_STRING;
	}
	assert(record->type==TYPE_STRING);
	LWW_register_set(record->string, value, value_sz);
	record->type=TYPE_STRING;
	return;
}
long long OR_record_set_int(struct OR_record *record, long long integer)
{
	long long result;
	if(record->type==TYPE_NONE)
	{
		record->counter=PN_counter_new(record->replica_id);
		record->type=TYPE_INTEGER;
	}
	assert(record->type==TYPE_INTEGER);
	result=PN_counter_set(record->counter, integer);
	return result;
}
long long OR_record_incrby(struct OR_record *record, long long increment)
{
	if(record->type==TYPE_NONE)
	{
		record->counter=PN_counter_new(record->replica_id);
		record->type=TYPE_INTEGER;
	}
	assert(record->type==TYPE_INTEGER);
	long long result=0;
	result=PN_counter_incrby(record->counter, increment);
	return result;
}
struct OR_map
{

	uuid_t my_id;
	unsigned long long sequence_generator;
	struct OR_record *hash_record;
	struct OR_timestamp *hash_replica_vector;
};
struct OR_map *OR_map_new(uuid_t my_uuid)
{
	struct OR_map *map=ALLOC(struct OR_map, 1);
	uuid_copy(map->my_id, my_uuid);
	return map;
}
void OR_map_free(struct OR_map *map)
{
	struct OR_record *record=NULL, *tmp_record=NULL;
	HASH_ITER(hh, map->hash_record, record, tmp_record)
	{
		HASH_DEL(map->hash_record, record);
		OR_record_free(record);
	}
	struct OR_timestamp *ts=NULL, *tmp_ts=NULL;
	HASH_ITER(hh, map->hash_replica_vector, ts, tmp_ts)
	{
		HASH_DEL(map->hash_replica_vector, ts);
		OR_timestamp_free(ts);
	}
	free(map);
	return;
}
static unsigned long long OR_map_step(struct OR_map *map)
{
	struct OR_timestamp *ts=NULL;
	unsigned long long new_sequence=map->sequence_generator++;

	HASH_FIND(hh, map->hash_replica_vector, map->my_id, sizeof(map->my_id), ts);
	if(!ts)
	{
		ts=OR_timestamp_new(map->my_id, new_sequence);	
		HASH_ADD_KEYPTR(hh, map->hash_replica_vector, ts->uuid, sizeof(ts->uuid), ts);
	}
	else
	{
		ts->sequence=new_sequence;
	}
	return new_sequence;
}
int OR_map_add(struct OR_map *map, const char *key, size_t key_len)
{
	struct OR_record *record=NULL;
	int ret=0;
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(!record)
	{
		record=OR_record_new(map->my_id, key, key_len, TYPE_NONE);
		HASH_ADD_KEYPTR(hh, map->hash_record, record->key, record->key_len, record);
		ret=1;
	}
	unsigned long long new_sequence=OR_map_step(map);
	OR_record_step(record, map->my_id, new_sequence);
	
	return ret;
}
int OR_map_set(struct OR_map *map, const char *key, size_t key_len, const char *value, size_t value_sz)
{
	struct OR_record *record=NULL;
	int ret=0;
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(!record)
	{
		record=OR_record_new(map->my_id, key, key_len, TYPE_NONE);
		HASH_ADD_KEYPTR(hh, map->hash_record, record->key, record->key_len, record);
		ret=1;
	}
	char *endptr=NULL;
	long long int_value=strtol(value, &endptr, 10);
	if(*endptr=='\0')
	{
		OR_record_set_int(record, int_value);
	}
	else
	{
		OR_record_set_str(record, value, value_sz);
	}
	
	unsigned long long new_sequence=OR_map_step(map);
	OR_record_step(record, map->my_id, new_sequence);
	
	return ret;
}

int OR_map_incrby(struct OR_map *map, const char *key, size_t key_len, long long increment, long long *result)
{
	struct OR_record *record=NULL;
	int ret=0;
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(!record)
	{		
		record=OR_record_new(map->my_id, key, key_len, TYPE_INTEGER);
		HASH_ADD_KEYPTR(hh, map->hash_record, record->key, record->key_len, record);
		ret=1;
	}
	if(record->type!=TYPE_INTEGER)
	{
		return -1;
	}
	
	*result=OR_record_incrby(record, increment);
	unsigned long long new_sequence=OR_map_step(map);
	OR_record_step(record, map->my_id, new_sequence);
	
	return ret;
}
enum record_copy_type
{
	copy_all,
	copy_key,
	copy_value
};

struct OR_map_kv *OR_map_kv_new(struct OR_record *record, enum record_copy_type type)
{
	struct OR_map_kv *kv=ALLOC(struct OR_map_kv, 1);
	kv->type=record->type;
	if(type==copy_all || type==copy_key)
	{
		kv->key_len=record->key_len;
		kv->key=ALLOC(char, kv->key_len);
		memcpy(kv->key, record->key, kv->key_len);
	}
	if(type==copy_all || type==copy_value)
	{
		switch(record->type)
		{
			case TYPE_STRING:
				LWW_register_get1(record->string, &kv->string, &kv->str_len);
				break;
			case TYPE_INTEGER:
				kv->integer=PN_counter_get(record->counter);
				break;
			default:
				break;
		}
	}
	return kv;
}
void OR_map_kv_free(struct OR_map_kv *kv)
{
	if(!kv)	return;
	free(kv->key);
	free(kv->string);
	kv->string=NULL;
	kv->integer=0;
	free(kv);
	return;
}
struct OR_map_kv *OR_map_get(struct OR_map *map, const char *key, size_t key_len)
{	
	struct OR_record *record=NULL;
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(!record)
	{
		return NULL;
	}
	struct OR_map_kv *kv=OR_map_kv_new(record, copy_value);
	return kv;
}
size_t OR_map_filter_keys(struct OR_map *map, OR_map_key_filter_cb filter_func, void *cb_arg, struct OR_map_kv ***kv_list)
{
	if(0==HASH_CNT(hh, map->hash_record))
	{
		return 0;
	}
	size_t list_sz=4;
	size_t n_kv=0;
	*kv_list=ALLOC(struct OR_map_kv*, list_sz);
	
	struct OR_record *record=NULL, *tmp=NULL;
	int ret=0;
	HASH_ITER(hh, map->hash_record, record, tmp)
	{
		ret=filter_func(record->key, record->key_len, cb_arg);
		if(ret)
		{
			if(n_kv==list_sz)
			{
				*kv_list=(struct OR_map_kv**)realloc(*kv_list, sizeof(struct OR_map_kv*)*list_sz*2);
				list_sz*=2;
			}
			(*kv_list)[n_kv]=OR_map_kv_new(record, copy_key);
			n_kv++;
		}
	}
	return n_kv;	
}
size_t OR_map_generic_traverse(struct OR_map *map, struct OR_map_kv ***kv_list, enum record_copy_type type)
{
	size_t n_kv=HASH_CNT(hh, map->hash_record);
	if(n_kv==0)
	{
		return 0;
	}
	*kv_list=ALLOC(struct OR_map_kv*, n_kv);
	
	struct OR_record *record=NULL, *tmp=NULL;
	size_t i=0;
	HASH_ITER(hh, map->hash_record, record, tmp)
	{
		(*kv_list)[i]=OR_map_kv_new(record, type);
		i++;
	}
	return n_kv;

}
size_t OR_map_get_all(struct OR_map *map, struct OR_map_kv ***kv_list)
{
	return OR_map_generic_traverse(map, kv_list, copy_all);
}
size_t OR_map_keys(struct OR_map *map, struct OR_map_kv ***kv_list)
{
	return OR_map_generic_traverse(map, kv_list, copy_key);
}
size_t OR_map_values(struct OR_map *map, struct OR_map_kv ***kv_list)
{
	return OR_map_generic_traverse(map, kv_list, copy_value);
}

int OR_map_remove(struct OR_map *map, const char *key, size_t key_len)
{
	struct OR_record *record=NULL;	
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(!record)
	{
		return 0;
	}
	HASH_DELETE(hh, map->hash_record, record);
	OR_record_free(record);
	return 1;
}
int OR_map_exisits(struct OR_map *map, const char* key, size_t key_len)
{	
	struct OR_record *record=NULL;
	HASH_FIND(hh, map->hash_record, key, key_len, record);
	if(record)
	{
		return 1;
	}
	return 0;
}

static int is_removed_by_peer(struct OR_record *record, const struct OR_timestamp *hash_peer_vector)
{
	struct OR_timestamp *ts=NULL, *tmp=NULL, *remote_ts=NULL;
	HASH_ITER(hh, record->hash_timestamp, ts, tmp)
	{
		HASH_FIND(hh, hash_peer_vector, ts->uuid, sizeof(ts->uuid), remote_ts);
		if(!remote_ts || ts->sequence > remote_ts->sequence)
		{
			continue;
		}
		HASH_DEL(record->hash_timestamp, ts);
		OR_timestamp_free(ts);
	}
	return record->hash_timestamp?1:0;
}
size_t OR_map_cardinality(struct OR_map *map)
{
	return HASH_COUNT(map->hash_record);
}
size_t OR_map_replica_number(struct OR_map *map)
{
	return HASH_COUNT(map->hash_replica_vector);
}
void OR_record_serialize(struct OR_record *record, mpack_writer_t *writer)
{
	struct OR_timestamp *ts=NULL, *tmp_ts=NULL;
	char *value_blob=NULL;
	size_t value_blob_sz=0;

	mpack_start_map(writer, 4);//Start a member
	mpack_write_cstr(writer, "k");
	mpack_write_str(writer, record->key, record->key_len);
	mpack_write_cstr(writer, "t");
	mpack_write_i32(writer, record->type);
	
	mpack_write_cstr(writer, "v");
	switch(record->type)
	{
		case TYPE_INTEGER:
			PN_counter_serialize(record->counter, &value_blob, &value_blob_sz);
			mpack_write_str(writer, value_blob, value_blob_sz);
			free(value_blob);				
			break;
		case TYPE_STRING:
			LWW_register_serialize(record->string, &value_blob, &value_blob_sz);
			mpack_write_str(writer, value_blob, value_blob_sz);			
			free(value_blob);				
			break;
		default:
			mpack_write_nil(writer);
			break;
	}
	
	mpack_write_cstr(writer, "ts");
	mpack_start_array(writer, HASH_CNT(hh, record->hash_timestamp)*2);//Start timestamps
	HASH_ITER(hh, record->hash_timestamp, ts, tmp_ts)
	{
		mpack_write_str(writer, (char*)ts->uuid, sizeof(uuid_t));
		mpack_write_u64(writer, ts->sequence);
	}
	mpack_finish_array(writer);//End timestamps
	
	mpack_finish_map(writer); //End a member
	return;
}
struct OR_record* OR_record_deserialize(mpack_node_t mpnode_record)
{
	mpack_node_t mpnode_k, mpnode_v, mpnode_ts_array;
	struct OR_record *record=NULL;
	record=ALLOC(struct OR_record, 1);
	
	mpnode_k=mpack_node_map_cstr(mpnode_record, "k");
	record->key_len=mpack_node_strlen(mpnode_k);		
	record->key=ALLOC(char, record->key_len);
	memcpy(record->key, mpack_node_str(mpnode_k), record->key_len);		
	record->type=mpack_node_int(mpack_node_map_cstr(mpnode_record, "t"));
	mpnode_v=mpack_node_map_cstr(mpnode_record, "v");
	switch(record->type)
	{
		case TYPE_STRING:
			record->string=LWW_register_deserialize(mpack_node_str(mpnode_v), mpack_node_strlen(mpnode_v));
			break;
		case TYPE_INTEGER:
			record->counter=PN_counter_deserialize(mpack_node_str(mpnode_v), mpack_node_strlen(mpnode_v));
			break;
		default:
			record->none=NULL;
			break;
	}
	
	
	mpnode_ts_array=mpack_node_map_cstr(mpnode_record, "ts");
	size_t n_ts=mpack_node_array_length(mpnode_ts_array);

	mpack_node_t mpnode_ts_replica, mpnode_ts_seq;
	struct OR_timestamp *ts=NULL;
	uuid_t ts_uuid;
	for(size_t i=0; i<n_ts; i+=2)
	{
		mpnode_ts_replica=mpack_node_array_at(mpnode_ts_array, i);
		mpnode_ts_seq=mpack_node_array_at(mpnode_ts_array, i+1);
		assert(mpack_node_strlen(mpnode_ts_replica)==sizeof(uuid_t));
		memcpy(ts_uuid, mpack_node_str(mpnode_ts_replica), sizeof(uuid_t));
		ts=OR_timestamp_new(ts_uuid, mpack_node_u64(mpnode_ts_seq));
		HASH_ADD_KEYPTR(hh, record->hash_timestamp, ts->uuid, sizeof(ts->uuid), ts);
	}
	return record;
}
static void OR_record_merge(struct OR_record *dst, const struct OR_record *src)
{
	struct OR_timestamp *src_ts=NULL, *dst_ts=NULL, *tmp=NULL;
	HASH_ITER(hh, src->hash_timestamp, src_ts, tmp)
	{
		HASH_FIND(hh, dst->hash_timestamp, src_ts->uuid, sizeof(uuid_t), dst_ts);
		if(dst_ts)
		{
			dst_ts->sequence=MAX(dst_ts->sequence, src_ts->sequence);
		}
		else
		{
			dst_ts=OR_timestamp_new(src_ts->uuid, src_ts->sequence);
			HASH_ADD_KEYPTR(hh, dst->hash_timestamp, dst_ts->uuid, sizeof(uuid_t), dst_ts);
		}
	}
	assert(dst->type==src->type);
	switch(dst->type)
	{
		case TYPE_STRING:
			LWW_register_merge(dst->string, src->string);
			break;
		case TYPE_INTEGER:
			PN_counter_merge(dst->counter, src->counter);
			break;
		default:
			break;
	}

	return;
}

void OR_map_serialize(struct OR_map *map, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;
	struct OR_record *record=NULL, *tmp_record=NULL;
	struct OR_timestamp *ts=NULL, *tmp_ts=NULL;
	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);	
	mpack_build_map(&writer);
	
	mpack_write_cstr(&writer, "kv");

	mpack_start_array(&writer, HASH_COUNT(map->hash_record));//Start kv list
	HASH_ITER(hh, map->hash_record, record, tmp_record)
	{
		OR_record_serialize(record, &writer);
	}
	mpack_finish_array(&writer);//End member list

	mpack_write_cstr(&writer, "rv");
	mpack_start_array(&writer, HASH_COUNT(map->hash_replica_vector)*2);//Start replica vector
	HASH_ITER(hh, map->hash_replica_vector, ts, tmp_ts)
	{
		mpack_write_str(&writer, (char*) ts->uuid, sizeof(uuid_t));
		mpack_write_i64(&writer, ts->sequence);
	}
	mpack_finish_array(&writer); //End replica vector

	mpack_complete_map(&writer);
	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the OR-Map!\n");
		return;
	}
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
}
struct OR_map *OR_map_deserialize(const char *blob, size_t blob_sz)
{
	struct OR_map *map=ALLOC(struct OR_map, 1);

	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);
	struct OR_record *record=NULL;
	struct OR_timestamp *ts=NULL;
	mpack_node_t mpnode_kv_array=mpack_node_map_cstr(root, "kv");
	size_t n_member=0, n_ts=0;
	n_member=mpack_node_array_length(mpnode_kv_array);
	mpack_node_t mpnode_ts_array, mpnode_ts_replica, mpnode_ts_seq;
	

	for(size_t i=0; i<n_member; i++)
	{
		record=OR_record_deserialize(mpack_node_array_at(mpnode_kv_array, i));		
		HASH_ADD_KEYPTR(hh, map->hash_record, record->key, record->key_len, record);		
	}

	mpnode_ts_array=mpack_node_map_cstr(root, "rv");
	n_ts=mpack_node_array_length(mpnode_ts_array);
	uuid_t ts_uuid;
	for(size_t i=0; i<n_ts; i+=2)
	{
		mpnode_ts_replica=mpack_node_array_at(mpnode_ts_array, i);
		mpnode_ts_seq=mpack_node_array_at(mpnode_ts_array, i+1);
		assert(mpack_node_strlen(mpnode_ts_replica)==sizeof(uuid_t));
		memcpy(ts_uuid, mpack_node_str(mpnode_ts_replica), sizeof(uuid_t));
		ts=OR_timestamp_new(ts_uuid, mpack_node_u64(mpnode_ts_seq));
		HASH_ADD_KEYPTR(hh, map->hash_replica_vector, ts->uuid, sizeof(uuid_t), ts);
	}

	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred decoding the OR-Map!\n");
	}
	return map;
}
void OR_map_merge(struct OR_map *dst, struct OR_map *src)
{
	struct OR_record *dst_record=NULL, *src_record=NULL, *tmp=NULL, *tmp_to_merge=NULL;
	HASH_ITER(hh, dst->hash_record, dst_record, tmp)
	{
		HASH_FIND(hh, src->hash_record, dst_record->key, dst_record->key_len, src_record);
		if(src_record)
		{
			OR_record_merge(dst_record, src_record);
			dst_record->should_merge_flag=1;
		}
		else
		{
			dst_record->should_merge_flag=is_removed_by_peer(dst_record, src->hash_replica_vector);
		}
	}
	HASH_ITER(hh, src->hash_record, src_record, tmp_to_merge)
	{
		HASH_FIND(hh, dst->hash_record, src_record->key, src_record->key_len, dst_record);
		if(dst_record)
		{
			assert(dst_record->should_merge_flag==1);
			continue;
		}
		src_record->should_merge_flag=is_removed_by_peer(src_record, dst->hash_replica_vector);
		if(src_record->should_merge_flag)
		{
			HASH_DEL(src->hash_record, src_record);
			HASH_ADD_KEYPTR(hh, dst->hash_record, src_record->key, src_record->key_len, src_record);
		}
	}
	HASH_ITER(hh, dst->hash_record, dst_record, tmp)
	{
		if(!dst_record->should_merge_flag)
		{
			HASH_DEL(dst->hash_record, dst_record);
			OR_record_free(dst_record);
		}
		else
		{
			dst_record->should_merge_flag=0;//reset flag
		}
	}
	struct OR_timestamp *ts=NULL, *to_merge_ts=NULL, *tmp_ts=NULL;
	HASH_ITER(hh, src->hash_replica_vector, to_merge_ts, tmp_ts)
	{
		HASH_FIND(hh, dst->hash_replica_vector, to_merge_ts->uuid, sizeof(uuid_t), ts);
		if(ts)
		{
			ts->sequence=MAX(ts->sequence, to_merge_ts->sequence);
		}
		else
		{
			HASH_DEL(src->hash_replica_vector, to_merge_ts);
			HASH_ADD_KEYPTR(hh, dst->hash_replica_vector, to_merge_ts->uuid, sizeof(uuid_t), to_merge_ts);
		}
	}
	return;
}
void OR_map_merge_blob(struct OR_map *map, const char *blob, size_t blob_sz)
{
	struct OR_map *to_merge=NULL;
	to_merge=OR_map_deserialize(blob, blob_sz);
	OR_map_merge(map, to_merge);
	OR_map_free(to_merge);
	return;
}

