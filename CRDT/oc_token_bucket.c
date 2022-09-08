#include "crdt_utils.h"
#include "pn_counter.h"
#include "uthash.h"
#include "mpack.h"

#include <assert.h>
#include <string.h>
#include <sys/time.h>

struct OC_configuration
{
	long long CIR;	//Committed Information Rate
	long long CBS;	//Committed Burst Size
	long long refill_duration_ms;
	struct timeval write_timestamp;
};
struct OC_token_bucket
{
	char* my_id;
	size_t my_id_sz;	
	struct OC_configuration cfg;
	long long refilled;
	struct timeval refill_timestamp;//no sync, local only
	struct PN_counter *consumed;
};
struct OC_token_bucket *OC_token_bucket_new(const char* id, size_t id_sz, long long CIR, long long CBS)
{
	struct OC_token_bucket *bucket=ALLOC(struct OC_token_bucket, 1);
	bucket->consumed=PN_counter_new(id, id_sz);
	gettimeofday(&bucket->cfg.write_timestamp, NULL);
	bucket->cfg.CIR=CIR;
	bucket->cfg.CBS=CBS;
	bucket->cfg.refill_duration_ms=10;
	gettimeofday(&bucket->refill_timestamp, NULL);
	bucket->refilled=bucket->cfg.CBS;
	return bucket;
}
void OC_token_bucket_free(struct OC_token_bucket *bucket)
{
	PN_counter_free(bucket->consumed);
	free(bucket);
}
#define	CONSUME_FLAG_DRYRUN	(1<<1)
#define	CONSUME_FLAG_FORCE	(1<<2)
static long long token_consume_generic(struct OC_token_bucket *bucket, long long tokens, int flag)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	long long delta_time_ms=timeval_delta_ms(bucket->refill_timestamp, now);
	long long consumed=PN_counter_get(bucket->consumed);
	long long to_add=0;
	long long refilled=bucket->refilled;
	
	if(delta_time_ms>bucket->cfg.refill_duration_ms)
	{
		to_add=bucket->cfg.CBS*delta_time_ms/1000;
		if(to_add + refilled - consumed < bucket->cfg.CBS)
		{
			refilled+=to_add;
		}
		else
		{
			refilled+=bucket->cfg.CBS - (refilled - consumed);
		}
	}
	if(bucket->refilled!=refilled && !(flag&CONSUME_FLAG_DRYRUN))
	{
		bucket->refilled=refilled;		
		memcpy(&bucket->refill_timestamp, &now, sizeof(bucket->refill_timestamp));
	}
	if(tokens<0)
	{
		return refilled-consumed;
	}
	else if(tokens  <= refilled-consumed || (flag&CONSUME_FLAG_FORCE))
	{
		PN_counter_incrby(bucket->consumed, tokens);
		return tokens;
	}
	else
	{
		return 0;
	}
}
long long OC_token_bucket_get_available(struct OC_token_bucket *bucket)
{
	return token_consume_generic(bucket, -1, CONSUME_FLAG_DRYRUN);
}
long long OC_token_bucket_consume(struct OC_token_bucket *bucket, long long tokens)
{
	return token_consume_generic(bucket, tokens, 0);
}
void OC_token_bucket_serialize(struct OC_token_bucket *bucket, char **blob, size_t *blob_sz)
{
	mpack_writer_t writer;
	char *mpack_buff=NULL;
	size_t mpack_sz=0;

	mpack_writer_init_growable(&writer, &mpack_buff, &mpack_sz);
	mpack_build_array(&writer);	
	mpack_write_bin(&writer, (const char*)&(bucket->cfg), sizeof(bucket->cfg));
	mpack_write_bin(&writer, (const char*)&(bucket->refill_timestamp), sizeof(bucket->refill_timestamp));
	mpack_write_i64(&writer, bucket->refilled);

	char *pnc_blob=NULL;
	size_t pnc_blob_sz=0;
	PN_counter_serialize(bucket->consumed, &pnc_blob, &pnc_blob_sz);
	mpack_write_bin(&writer, pnc_blob, pnc_blob_sz);
	free(pnc_blob);
	pnc_blob=NULL;
	
	mpack_complete_array(&writer);
	mpack_writer_destroy(&writer);
	
	*blob=mpack_buff;
	*blob_sz=mpack_sz;
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
	if(dst->refilled < src->refilled)//Bigger refilled wins
	{
		dst->refilled=src->refilled;
		memcpy(&dst->refill_timestamp, &src->refill_timestamp, sizeof(dst->refill_timestamp));
	}
	PN_counter_merge(dst->consumed, src->consumed);
	return;
}
void OC_token_bucket_merge_blob(struct OC_token_bucket *bucket, const char *blob, size_t blob_sz)
{
	struct OC_token_bucket *to_merge=OC_token_bucket_deserialize(blob, blob_sz);
	OC_token_bucket_merge(bucket, to_merge);
	OC_token_bucket_free(to_merge);
	return;
}

