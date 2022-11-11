#include "oc_token_bucket.h"
#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>

struct OC_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_duration_ms;
	struct timeval write_timestamp;
};
struct OC_token_bucket
{
	struct OC_configuration cfg;
	long long refilled;
	struct timeval refill_timestamp;
	struct PN_counter *consumed;
};
struct OC_token_bucket *OC_token_bucket_new(uuid_t my_id, struct timeval now, long long CIR, long long CBS)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	bucket->consumed=PN_counter_new(my_id);
	gettimeofday(&bucket->cfg.write_timestamp, NULL);
	memcpy(&bucket->cfg.write_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	bucket->cfg.CIR=CIR;
	bucket->cfg.CBS=CBS;
	bucket->cfg.refill_duration_ms=10;
	memcpy(&bucket->refill_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	bucket->refilled=bucket->cfg.CBS;
	return bucket;
}


void OC_token_bucket_free(struct OC_token_bucket *bucket)
{
	PN_counter_free(bucket->consumed);
	free(bucket);
}
void OC_token_bucket_configure(struct OC_token_bucket *bucket, struct timeval now, long long CIR, long long CBS, long long refill_duration_ms)
{
	memcpy(&bucket->cfg.write_timestamp, &now, sizeof(bucket->cfg.write_timestamp));
	if(CIR>=0) bucket->cfg.CIR=CIR;
	if(CBS>=0) bucket->cfg.CBS=CBS;
	if(refill_duration_ms>0) bucket->cfg.refill_duration_ms=refill_duration_ms;
}


long long OC_token_bucket_control(struct OC_token_bucket *bucket, struct timeval now, enum OC_token_bucket_command cmd, long long tokens)
{
	long long delta_time_ms=timeval_delta_ms(bucket->refill_timestamp, now);
	long long consumed=PN_counter_get(bucket->consumed);
	long long to_add=0;
	long long refilled=bucket->refilled;
	assert(refilled>=0);
	assert(consumed>=0);
	int refill_flag=0;
	
	if(delta_time_ms>=bucket->cfg.refill_duration_ms)
	{
		to_add=bucket->cfg.CIR*delta_time_ms/1000;
		if(refilled<consumed)
		{
			refilled=consumed;
		}
		if(to_add + refilled - consumed < bucket->cfg.CBS)
		{
			refilled+=to_add;
		}
		else
		{
			refilled+=bucket->cfg.CBS - (refilled - consumed);
		}
		refill_flag=1;
	}
	size_t n_replica=PN_counter_replica_num(bucket->consumed);

	
	long long global_available=MAX(refilled-consumed, 0);
	long long reserved=bucket->cfg.CIR*(n_replica-1)/n_replica;
	long long local_available=MAX(global_available-reserved, 0);
//	long long local_available=global_available;
	long long assigned=0;
	switch(cmd)
	{
		case OCTB_CMD_CONSUME_AS_MUCH_AS_POSSIBLE:
			assigned=global_available;
			break;
		case OCTB_CMD_CONSUME_FORCE:
			assigned=tokens;
			break;
		case OCTB_CMD_CONSUME_FLEXIBLE:			
			assigned=MIN(tokens, local_available);
			break;
		case OCTB_CMD_CONSUME_NORMAL:
			assigned=(tokens<=local_available) ? tokens:0;
			break;
		case OCTB_CMD_READ_AVAILABLE:
			return global_available;
		case OCTB_CMD_READ_REFILLED:
			return refilled;
		case OCTB_CMD_READ_CONSUEMD:
			return consumed;
		default:
			assert(0);
			break;
	}
	if(refill_flag)
	{
		bucket->refilled=refilled;		
		memcpy(&bucket->refill_timestamp, &now, sizeof(bucket->refill_timestamp));
	}
	assert(assigned>=0);
	PN_counter_incrby(bucket->consumed, assigned);
	return assigned;
}
long long OC_token_bucket_read_refilled(struct OC_token_bucket *bucket)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return OC_token_bucket_control(bucket, now, OCTB_CMD_READ_REFILLED, 0);

}
long long OC_token_bucket_read_available(struct OC_token_bucket *bucket)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return OC_token_bucket_control(bucket, now, OCTB_CMD_READ_AVAILABLE, 0);
}
void OC_token_bucket_info(struct OC_token_bucket *bucket, struct OC_token_bucket_info *info)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	info->CIR=bucket->cfg.CIR;
	info->CBS=bucket->cfg.CBS;
	info->refill_duration_ms=bucket->cfg.refill_duration_ms;
	info->consumed=OC_token_bucket_control(bucket, now, OCTB_CMD_READ_CONSUEMD, 0);
	info->refilled=OC_token_bucket_control(bucket, now, OCTB_CMD_READ_REFILLED, 0);
	info->available=OC_token_bucket_control(bucket, now, OCTB_CMD_READ_AVAILABLE, 0);
	return;
}

void OC_token_bucket_serialize(struct OC_token_bucket *bucket, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;



	char *pnc_blob=NULL;
	size_t pnc_blob_sz=0;
	PN_counter_serialize(bucket->consumed, &pnc_blob, &pnc_blob_sz);



	size_t mpack_sz=128+pnc_blob_sz+sizeof(bucket->cfg)+sizeof(bucket->refill_timestamp)+sizeof(long long);
	char *mpack_buff=(char*)malloc(mpack_sz);

	mpack_writer_init(&writer, mpack_buff, mpack_sz);
	mpack_build_array(&writer);	
	mpack_write_bin(&writer, (const char*)&(bucket->cfg), sizeof(bucket->cfg));
	mpack_write_bin(&writer, (const char*)&(bucket->refill_timestamp), sizeof(bucket->refill_timestamp));
	mpack_write_i64(&writer, bucket->refilled);
	mpack_write_bin(&writer, pnc_blob, pnc_blob_sz);

	
	mpack_complete_array(&writer);
	*blob_sz=mpack_writer_buffer_used(&writer);
	if(mpack_ok!=mpack_writer_destroy(&writer))
	{
		assert(0);
	}
	*blob=mpack_buff;

	free(pnc_blob);
	pnc_blob=NULL;
	return;
}
struct OC_token_bucket *OC_token_bucket_deserialize(const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, blob_sz);
	mpack_tree_parse(&tree);
	mpack_node_t root=mpack_tree_root(&tree);

	size_t array_sz=mpack_node_array_length(root);
	assert(array_sz==4);

	size_t i=0;
	mpack_node_t tmp_node;
	tmp_node=mpack_node_array_at(root, i);
	assert(mpack_node_bin_size(tmp_node)==sizeof(bucket->cfg));
	memcpy(&bucket->cfg, mpack_node_bin_data(tmp_node), mpack_node_bin_size(tmp_node));
	i++;

	tmp_node=mpack_node_array_at(root, i);
	assert(mpack_node_bin_size(tmp_node)==sizeof(bucket->refill_timestamp));
	memcpy(&bucket->refill_timestamp, mpack_node_bin_data(tmp_node), mpack_node_bin_size(tmp_node));
	i++;

	tmp_node=mpack_node_array_at(root, i);
	bucket->refilled=mpack_node_i64(tmp_node);
	i++;
	
	tmp_node=mpack_node_array_at(root, i);
	bucket->consumed=PN_counter_deserialize(mpack_node_bin_data(tmp_node), mpack_node_bin_size(tmp_node));
	i++;

	if (mpack_tree_destroy(&tree) != mpack_ok) 
	{
		fprintf(stderr, "An error occurred decoding the OC_token_bucket!\n");
	}	
	return bucket;
}
void OC_token_bucket_merge(struct OC_token_bucket *dst, struct OC_token_bucket *src)
{
	if(timercmp(&(dst->cfg.write_timestamp), &(src->cfg.write_timestamp), <))//Last-Write-Wins
	{
		memcpy(&dst->cfg, &src->cfg, sizeof(src->cfg));
	}
	PN_counter_merge(dst->consumed, src->consumed);
	
	assert(dst->refilled>=0 && src->refilled>=0);
	if(dst->refilled < src->refilled)//Bigger refilled wins
	{
		dst->refilled=src->refilled;
		memcpy(&dst->refill_timestamp, &src->refill_timestamp, sizeof(dst->refill_timestamp));
	}	
	return;
}
void OC_token_bucket_merge_blob(struct OC_token_bucket *bucket, const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *to_merge=OC_token_bucket_deserialize(blob, blob_sz);
	OC_token_bucket_merge(bucket, to_merge);
	OC_token_bucket_free(to_merge);
	return;
}

