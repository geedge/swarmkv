#include "oc_token_bucket.h"
#include "fair_token_bucket.h"
#include "bulk_token_bucket.h"

#include "crdt_utils.h"

#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>
#include <math.h>


void OC_token_bucket_sync(struct OC_token_bucket *list[], size_t n)
{
	char *blob=NULL;
	size_t blob_sz=0;
	
	for(size_t i=0; i<n; i++)
	{
		OC_token_bucket_serialize(list[i], &blob, &blob_sz);
		for(size_t j=0; j<n; j++)
		{
			if(j==i) continue;
			OC_token_bucket_merge_blob(list[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	return;
}

enum traffic_type 
{
	LIGHT_UNIFORM_TYPE,
	LIGHT_TWO_EIGHT_TYPE,
	HEAVY_TWO_EIGHT_TYPE,
	HEAVY_UNIFORM_EXTREME_TYPE
};
const int REPLICA_NUMBER=3;
long long get_request_tokens(int index, enum traffic_type type, long long step_us, long long CIR, long long CBS) 
{
	
	long long request_size=0;
	long long standard = CIR * step_us / 1000000;
	long long sd10 = floor((long double)standard * 0.1);
	int eight_replica_num = floor(REPLICA_NUMBER * 0.8);
	long long rand_sd10 = random() % sd10;
	int scope_flag = FALSE;
	if (random() % 2) scope_flag = TRUE;
	switch (type) 
	{
		case LIGHT_UNIFORM_TYPE:
			request_size = (long long)floor((long double)standard * 0.5);
			if (scope_flag) {
			request_size += rand_sd10;
			} else {
			request_size -= rand_sd10;
			}
			break;
		case LIGHT_TWO_EIGHT_TYPE:
			if (index < eight_replica_num && scope_flag) {
			request_size = (long long)floor((long double)standard * 0.2) + rand_sd10;
			} else if (index < eight_replica_num && !scope_flag) {
			request_size = (long long)floor((long double)standard * 0.2) - rand_sd10;
			} else if (index >= eight_replica_num && scope_flag) {
			request_size = (long long)floor((long double)standard * 1.6) + rand_sd10;
			} else {
			request_size = (long long)floor((long double)standard * 1.6) - rand_sd10;
			}
			break;
		case HEAVY_TWO_EIGHT_TYPE:
			if (index < eight_replica_num && scope_flag) {
			request_size = (long long)floor((long double)standard * 0.6) + rand_sd10;
			} else if (index < eight_replica_num && !scope_flag) {
			request_size = (long long)floor((long double)standard * 0.6) - rand_sd10;
			} else if (index >= eight_replica_num && scope_flag) {
			request_size = (long long)floor((long double)standard * 4.8) + rand_sd10;
			} else {
			request_size = (long long)floor((long double)standard * 4.8) - rand_sd10;
			}
			break;
		case HEAVY_UNIFORM_EXTREME_TYPE:
			request_size = (long long)floor((long double)standard * 2);
			if (scope_flag) {
			request_size += rand_sd10;
			} else {
			request_size -= rand_sd10;
			}
			break;
		default:
			break;
	}
	return request_size;
}

void traffic_distribution(traffic_type type)
{
	struct OC_token_bucket *buckets[REPLICA_NUMBER];
	size_t i = 0, j = 0;
	long long CIR = 1*1024*1024;
	long long CBS = 2*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for (i = 0; i < REPLICA_NUMBER; i++) 
	{
		uuid_generate(uuid);
		buckets[i] = OC_token_bucket_new(uuid, start, CIR, CBS);
	}
	long long tokens = 0, flexible_tokens = 0;
	long long consumed = 0, requested = 0, upper_limit = 0, refilled = 0;
	long long mimic_duration_us = (long long)100*1000*1000;
	long long step_us = 100;
	struct timeval step, now;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec = 0;
	step.tv_usec = (suseconds_t)step_us;
	for (i = 0; (long long)i < mimic_duration_us / step_us; i++) 
	{
		j = i % 3; // sequence selection
		timeradd(&now, &step, &now);
		tokens = get_request_tokens(j, type, step_us, CIR, CBS);
		flexible_tokens = OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_FLEXIBLE, tokens);
		requested += tokens;
		consumed += flexible_tokens;
		if(i%100==0)
		{
			OC_token_bucket_sync(buckets, REPLICA_NUMBER);
		}
	}
	upper_limit = CBS + CIR * timeval_delta_ms(start, now) / 1000;
	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled=info.refilled;
	EXPECT_LE(consumed, requested);
	double accuracy = (double)consumed / MIN(refilled, requested);
	EXPECT_NEAR(accuracy, 1, 0.01);
	printf("accuracy:%f, upper_limit:%lld, refilled:%lld, requested:%lld, consumed:%lld\n",
			accuracy, upper_limit, info.refilled, requested, consumed);
	for(i = 0; i < REPLICA_NUMBER; i++) 
	{
		OC_token_bucket_free(buckets[i]);
	}
}

TEST(OCTokenBucket, TrafficTypeConsumer)
{
	traffic_distribution(LIGHT_UNIFORM_TYPE);
	traffic_distribution(LIGHT_TWO_EIGHT_TYPE);
	traffic_distribution(HEAVY_TWO_EIGHT_TYPE);
	traffic_distribution(HEAVY_UNIFORM_EXTREME_TYPE);
}

TEST(OCTokenBucket, Basic)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct OC_token_bucket *bucket=NULL;
	long long CIR=100;
	long long CBS=200;
	struct timeval now;
	gettimeofday(&now, NULL);

	bucket=OC_token_bucket_new(uuid, now, CIR, CBS);
	long long tokens=0;

	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 140);
	EXPECT_EQ(tokens, 140);
	
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 61);
	EXPECT_EQ(tokens, 0);

	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 60);
	EXPECT_EQ(tokens, 60);

	now.tv_sec++;
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 90);
	EXPECT_EQ(tokens, 90);

	struct OC_token_bucket_info info;
	OC_token_bucket_info(bucket, now, &info);
	EXPECT_GE(info.available, 10);
//	printf("avail=%lld\n", tokens);
	
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 10);
	EXPECT_EQ(tokens, 10);
	OC_token_bucket_free(bucket);
}
TEST(OCTokenBucket, Serialize)
{
	uuid_t uuid;
	int n_replica=4;
	long long CIR=100;
	long long CBS=200;
	struct timeval now;
	gettimeofday(&now, NULL);
	struct OC_token_bucket *b[n_replica], *b1=NULL;
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		b[i]=OC_token_bucket_new(uuid, now, CIR, CBS);
		OC_token_bucket_consume(b[i], now, TB_CMD_CONSUME_NORMAL, 10);
	}
	OC_token_bucket_sync(b, n_replica);
	size_t sz=OC_token_bucket_serialized_size(b[0]);
	char *blob=NULL;
	size_t blob_sz=0;
	OC_token_bucket_serialize(b[0], &blob, &blob_sz);
	EXPECT_EQ(sz, blob_sz);
	b1=OC_token_bucket_deserialize(blob, blob_sz);
	free(blob);
	sz=OC_token_bucket_serialized_size(b1);
	EXPECT_EQ(sz, blob_sz);

	for(int i=0; i<n_replica; i++)
	{
		OC_token_bucket_free(b[i]);
	}
	OC_token_bucket_free(b1);
}
TEST(OCTokenBucket, Boundary)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct timeval now;
	gettimeofday(&now, NULL);
	struct OC_token_bucket *bucket=NULL;
	long long tokens=0, consumed=0;

	//Zero CIR
	bucket=OC_token_bucket_new(uuid, now, 0, 1000);
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 1000);
	EXPECT_EQ(tokens, 1000);

	now.tv_sec++;
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 1);
	EXPECT_EQ(tokens, 0);
	OC_token_bucket_free(bucket);

	//Zero CBS
	bucket=OC_token_bucket_new(uuid, now, 1000, 0);
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 1);
	EXPECT_EQ(tokens, 0);
	OC_token_bucket_free(bucket);

	//Infinite Tokens
	bucket=OC_token_bucket_new(uuid, now, 0, 0);
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 140);
	consumed+=tokens;
	EXPECT_EQ(tokens, 140);


	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 65535);
	consumed+=tokens;
	EXPECT_EQ(tokens, 65535);

	struct OC_token_bucket_info info;
	
	OC_token_bucket_info(bucket, now, &info);
	EXPECT_EQ(info.consumed, consumed);
	EXPECT_EQ(info.refilled, consumed);
	
	now.tv_sec++;
	OC_token_bucket_configure(bucket, now, 100, 500, 10);
	
	now.tv_sec+=500/100;
	tokens=OC_token_bucket_consume(bucket, now, TB_CMD_CONSUME_NORMAL, 500);
	consumed+=tokens;
	EXPECT_EQ(tokens, 500);

	OC_token_bucket_info(bucket, now, &info);
	EXPECT_EQ(info.consumed, consumed);
	EXPECT_EQ(info.refilled, consumed);

	OC_token_bucket_free(bucket);
}
TEST(OCTokenBucket, Merge)
{
	struct OC_token_bucket *buckets[2];
	uuid_t uuid;
	long long CIR=100;
	long long CBS=200;
	struct timeval now;
	gettimeofday(&now, NULL);

	size_t i=0;
	for(i=0; i<2; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, now, CIR, CBS);
	}
	long long tokens=0;
	OC_token_bucket_sync(buckets, 2);
	tokens=OC_token_bucket_consume(buckets[0], now, TB_CMD_CONSUME_NORMAL, 130);
	EXPECT_EQ(tokens, 130);
	tokens=OC_token_bucket_consume(buckets[1], now, TB_CMD_CONSUME_NORMAL, 30);
	EXPECT_EQ(tokens, 30);

	OC_token_bucket_sync(buckets, 2);
	now.tv_sec++;
	//each has 200-130-30+100/2=90 avaliable tokens
	tokens=OC_token_bucket_consume(buckets[0], now, TB_CMD_CONSUME_NORMAL, 91);
	EXPECT_EQ(tokens, 0);
	tokens=OC_token_bucket_consume(buckets[1], now, TB_CMD_CONSUME_FLEXIBLE, 90);
	EXPECT_EQ(tokens, 90);
	OC_token_bucket_sync(buckets, 2);
	
	for(i=0; i<2; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
double OC_token_bucket_test(size_t replica_num, long long mimic_duration_s, long long sync_interval_ms, int saturation_percent)
{
	struct OC_token_bucket *buckets[replica_num];
	size_t i=0, j=0;
	long long CIR=512*1024;
	long long CBS=2*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<replica_num; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long step_us=100, mimic_duration_us=mimic_duration_s*1000*1000;
	long long sync_interval_us=sync_interval_ms*1000;
	struct timeval step, now;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);
		j=random()%replica_num;
		tokens=CIR*step_us/(1000*1000);
		tokens=tokens*saturation_percent/100;
		tokens=tokens/2+random()%tokens;
		requested+=tokens;
		consumed+=OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_NORMAL, tokens);
		if((step_us*i)%sync_interval_us==0)
		{
			OC_token_bucket_sync(buckets, replica_num);
		}
	}
	upper_limit=CBS+CIR*timeval_delta_ms(start, now)/1000;

	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled=info.refilled;

	EXPECT_NEAR((double)consumed/MIN(refilled, requested), 1, 0.03);
//	EXPECT_LE(consumed, requested);
	double accuracy=(double)consumed/MIN(upper_limit, requested);
	double real_saturation=(double)requested/upper_limit;
	EXPECT_NEAR(real_saturation, (double)saturation_percent/100, 0.1);
	for(i=0; i<replica_num; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
	return accuracy;
}
TEST(OCTokenBucket, HeavyConsumer)
{
	double accuracy=0.0;
	long long replica_num=0, test_duration_s=500, sync_interval_ms=0;

	long long i=0, j=0;
	for(i=8; i<9; i++)
	{
		replica_num=i+1;
		for(j=0; j<10; j++)
		{
			sync_interval_ms=100*(j+1);
			accuracy=OC_token_bucket_test(replica_num, test_duration_s, sync_interval_ms, 200);
			printf("replica_num=%lld, sync_interval_ms=%lld, accuracy=%.4f\n", replica_num, sync_interval_ms, accuracy);
			EXPECT_NEAR(accuracy, 1, 0.05);
		}
	}
}
TEST(OCTokenBucket, LightConsumer)
{	
	double accuracy=0.0;
	long long replica_num=0, test_duration_s=100, sync_interval_ms=0;

	long long i=0, j=0;
	for(i=0; i<4; i++)
	{
		replica_num=i+1;
		for(j=0; j<20; j++)
		{
			sync_interval_ms=100*(j+1);
			accuracy=OC_token_bucket_test(replica_num, test_duration_s, sync_interval_ms, 90);
			EXPECT_NEAR(accuracy, 1, 0.03);
		}
	}
}
TEST(OCTokenBucket, ConcurrentHeavyConsumer)
{
	struct OC_token_bucket *buckets[REPLICA_NUMBER];
	size_t i=0, j=0;
	long long CIR=100*1024*1024;
	long long CBS=1000*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)100*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);		
		tokens=CIR*step_us/(1000*1000);		
		tokens=tokens/REPLICA_NUMBER+random()%tokens;
		for(j=0; j<REPLICA_NUMBER; j++)
		{
			requested+=tokens;			
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_FLEXIBLE, tokens);
		}
		if(i%100==0)
		{
			OC_token_bucket_sync(buckets, REPLICA_NUMBER);
		}
	}
	upper_limit=CBS+CIR*timeval_delta_ms(start, now)/1000;
	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled=info.refilled;

	EXPECT_NEAR((double)consumed/MIN(refilled, requested), 1, 0.01);
	EXPECT_NEAR((double)refilled/upper_limit, 1, 0.01);
	EXPECT_LE(consumed, requested);
	double accuracy=(double)consumed/MIN(upper_limit, requested);
	EXPECT_NEAR(accuracy, 1, 0.01);
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
TEST(OCTokenBucket, Reconfigure)
{
	struct OC_token_bucket *buckets[REPLICA_NUMBER];
	size_t i=0, j=0;
	double accuracy=0.0;
	long long CIR=100*1024*1024;
	long long CBS=1000*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)1000*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);		
		tokens=CIR*step_us/(1000*1000);		
		tokens=tokens/REPLICA_NUMBER+random()%tokens;
		for(j=0; j<REPLICA_NUMBER; j++)
		{
			requested+=tokens;			
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_FLEXIBLE, tokens);
		}
		if(i%100==0)
		{
			OC_token_bucket_sync(buckets, REPLICA_NUMBER);
		}
	}
	upper_limit=CBS+CIR*timeval_delta_ms(start, now)/1000;
	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled=info.refilled;
	EXPECT_NEAR((double)consumed/MIN(refilled, requested), 1, 0.01);
	EXPECT_NEAR((double)refilled/upper_limit, 1, 0.01);
	EXPECT_LE(consumed, requested);
	accuracy=(double)consumed/MIN(upper_limit, requested);
	EXPECT_NEAR(accuracy, 1, 0.01);

	memcpy(&start, &now, sizeof(start));
	CIR*=2;
	CBS*=5;
	requested=0;
	consumed=0;
	OC_token_bucket_configure(buckets[0], start, CIR, CBS, 0);
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);		
		tokens=CIR*step_us/(1000*1000);		
		tokens=tokens/REPLICA_NUMBER+random()%tokens;
		for(j=0; j<REPLICA_NUMBER; j++)
		{
			requested+=tokens;			
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_FLEXIBLE, tokens);
		}
		if(i%100==0)
		{
			OC_token_bucket_sync(buckets, REPLICA_NUMBER);
		}
	}
	upper_limit=CIR*timeval_delta_ms(start, now)/1000;//not + CBS, becasue reconfiguration will not refill the bucket to full.

	OC_token_bucket_info(buckets[0], now, &info);
	refilled = info.refilled - refilled;

	EXPECT_NEAR((double)consumed/MIN(refilled, requested), 1, 0.01);
	EXPECT_NEAR((double)refilled/upper_limit, 1, 0.01);
	EXPECT_LE(consumed, requested);
	accuracy=(double)consumed/MIN(upper_limit, requested);
	EXPECT_NEAR(accuracy, 1, 0.01);
	
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
TEST(OCTokenBucket, PartitionTolerance)
{
	struct OC_token_bucket *buckets[REPLICA_NUMBER];
	size_t i=0, j=0;
	long long CIR=1*1024*1024;
	long long CBS=2*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)100*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);
		j=random()%REPLICA_NUMBER;
		tokens=CIR*step_us/(1000*1000);
		tokens=tokens+random()%tokens;
		for(j=0; j<REPLICA_NUMBER; j++)
		{
			requested+=tokens;			
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CMD_CONSUME_FLEXIBLE, tokens);
		}
		//network partition at time (t/3, t*2/3]
		if((long long)i>mimic_duration_us/step_us/3 && (long long)i<= 2*mimic_duration_us/step_us/3)
		{
			continue;
		}
		//if(i%100==0)
		{
			OC_token_bucket_sync(buckets, REPLICA_NUMBER);
		}
	}
	upper_limit=CBS+(CIR*timeval_delta_ms(start, now)/1000)*2/3 + (CIR*timeval_delta_ms(start, now)/1000)*REPLICA_NUMBER/3;
	
	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled = info.refilled;

	EXPECT_NEAR((double)consumed/MIN(refilled, requested), 1, 0.01);
	EXPECT_LE(consumed, requested);
	double accuracy=(double)consumed/MIN(upper_limit, requested);
	EXPECT_NEAR(accuracy, 1, 0.01);
	for(i=0; i<REPLICA_NUMBER; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}

}
struct sftb_class
{
	long long class_id;
	long long weight;
	long long requested_CIR;
	long long demand_tokens;
	long long allocated_tokens;
	long long ideal_tokens;
};

int cmp_sftb_class(const void *a, const void *b)
{
	struct sftb_class *ra=(struct sftb_class*)a;
	struct sftb_class *rb=(struct sftb_class*)b;
	return (int)(ra->demand_tokens-rb->demand_tokens);
}
double max_min_fairness_index(long long available_tokens, struct sftb_class * classes, size_t n_class)
{
	qsort(classes, n_class, sizeof(struct sftb_class), cmp_sftb_class);
	long long total_weight=0;
	for(size_t i=0; i<n_class; i++)
	{
		total_weight+=classes[i].weight;
	}
	long long left_tokens=available_tokens;
	long long left_weight=total_weight;
	size_t n_satisfied=0;

	while(n_satisfied<n_class && left_tokens/left_weight>0 )
	{
		long long share=left_tokens/left_weight;
		for(size_t i=0; i<n_class; i++)
		{
			long long my_share=classes[i].weight*share;
			if(classes[i].demand_tokens == classes[i].ideal_tokens)
			{
				continue;
			}
			else if(classes[i].demand_tokens - classes[i].ideal_tokens <= my_share)
			{
				left_tokens -= (classes[i].demand_tokens - classes[i].ideal_tokens);
				classes[i].ideal_tokens=classes[i].demand_tokens;
			}
			else
			{
				left_tokens -= my_share;
				classes[i].ideal_tokens+=my_share;
				
			}
		}
		left_weight=0;
		n_satisfied=0;
		for(size_t i=0; i<n_class; i++)
		{
			if(classes[i].demand_tokens == classes[i].ideal_tokens)
			{
				n_satisfied++;
				continue;
			}
			left_weight += classes[i].weight;
			
		}
	}
	double index=0;
	for(size_t i=0; i<n_class; i++)
	{
		index += pow((double)(classes[i].ideal_tokens-classes[i].allocated_tokens)/classes[i].ideal_tokens, 2);
	}
	index=1-sqrt(index/n_class);

	return index;
}
void ftb_sync(struct fair_token_bucket **ftb, size_t n_ftb)
{
	char *blob=NULL;
	size_t blob_sz=0;
	for(size_t i=0; i<n_ftb; i++)
	{
		for(size_t j=0; j<n_ftb; j++)
		{
			if(i==j) continue;
			fair_token_bucket_serialize(ftb[j], &blob, &blob_sz);
			fair_token_bucket_merge_blob(ftb[i], blob, blob_sz);
			free(blob);
			blob=NULL;
		}
	}
	return;
}
double test_fair_token_bucket(struct sftb_class *classes, size_t n_class, long long CIR, long long CBS, int duration_s, int n_replica)
{
	uuid_t uuid;
	uuid_generate(uuid);
	struct timeval start, step, now;
	int step_us=1000;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	struct fair_token_bucket *ftb[n_replica];
	for(int i=0; i<n_replica; i++)
	{
		ftb[i]=fair_token_bucket_new(uuid, now, CIR, CBS);
	}
	int sync_interval_ms=400;
	for(int i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		int k=random()%n_class;
		int r=random()%n_replica;
		for(size_t j=0; j<n_class; j++)
		{
			int idx=(j+k)%n_class;
			long long this_demand=classes[idx].requested_CIR*step_us/(1000*1000);
			classes[idx].allocated_tokens+=fair_token_bucket_consume(ftb[r], now, 
								(const char*) &(classes[idx].class_id),
								sizeof(classes[idx].class_id),
								classes[idx].weight,
								TB_CMD_CONSUME_NORMAL,
								this_demand);
			classes[idx].demand_tokens+=this_demand;
		}
		if(0==i%(sync_interval_ms*1000/step_us))
		{
			ftb_sync(ftb, n_replica);
		}
	}
	long long available_tokens=CIR*duration_s+CBS;
	double index=max_min_fairness_index(available_tokens, classes, n_class);
	int print=0;
	if(print)
	{
		printf("class\tweight\tdemand\tallocated\tideal\r\n");
		for(size_t i=0; i<n_class; i++)
		{
			if((double)classes[i].allocated_tokens/classes[i].ideal_tokens< 1.1 &&
			(double) classes[i].allocated_tokens/classes[i].ideal_tokens > 0.9)
				continue;
			printf("%lld\t%lld\t%lld\t%lld\t%lld\r\n", classes[i].class_id,
										classes[i].weight,
										classes[i].demand_tokens/duration_s,
										classes[i].allocated_tokens/duration_s,
										classes[i].ideal_tokens/duration_s);
		}
		printf("CIR %lld fairness index %f\r\n", CIR, index);
	}
	for(int i=0; i<n_replica; i++)
	{
		fair_token_bucket_free(ftb[i]);
	}
	return index;
}

TEST(FairTokenBucket, Basic)
{
	double index=0;
	long long duration_s=350;
	struct sftb_class one_heavy_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 1, 20000, 0, 0, 0},
								{3, 1, 20000, 0, 0, 0},
								{4, 1, 20000, 0, 0, 0},
								{5, 1, 50000, 0, 0, 0}};
	
	index=test_fair_token_bucket(one_heavy_classes, 1, 120000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class all_light_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 1, 20000, 0, 0, 0},
								{3, 1, 20000, 0, 0, 0},
								{4, 1, 20000, 0, 0, 0},
								{5, 1, 20000, 0, 0, 0}};
	
	index=test_fair_token_bucket(all_light_classes, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class two_heavy_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 1, 20000, 0, 0, 0},
								{3, 1, 50000, 0, 0, 0},
								{400, 1, 50000, 0, 0, 0},
								{5, 1, 20003, 0, 0, 0}};
	
	index=test_fair_token_bucket(two_heavy_classes, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class all_heavy_classes[5]={{1, 1, 40000, 0, 0, 0}, 
								{2, 1, 40000, 0, 0, 0},
								{3, 1, 50000, 0, 0, 0},
								{4, 1, 60000, 0, 0, 0},
								{5, 1, 40000, 0, 0, 0}};
	
	index=test_fair_token_bucket(all_heavy_classes, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);
	
}
TEST(FairTokenBucket, Replicas)
{
	double index=0;
	long long duration_s=350;
	struct sftb_class one_heavy_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 1, 20000, 0, 0, 0},
								{3, 1, 20000, 0, 0, 0},
								{4, 1, 20000, 0, 0, 0},
								{5, 1, 50000, 0, 0, 0}};
	
	index=test_fair_token_bucket(one_heavy_classes, 1, 120000, 200000, duration_s, 2);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class two_heavy_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 1, 20000, 0, 0, 0},
								{3, 1, 50000, 0, 0, 0},
								{400, 1, 50000, 0, 0, 0},
								{5, 1, 20003, 0, 0, 0}};
	
	index=test_fair_token_bucket(two_heavy_classes, 5, 100000, 200000, duration_s, 2);
	EXPECT_NEAR(index, 1, 0.02);

}
TEST(FairTokenBucket, Weight)
{
	double index=0;
	long long duration_s=500;
	struct sftb_class one_heavy_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 2, 20000, 0, 0, 0},
								{3, 3, 20000, 0, 0, 0},
								{4, 4, 30000, 0, 0, 0},
								{5, 5, 50000, 0, 0, 0}};
	
	index=test_fair_token_bucket(one_heavy_classes, 5, 120000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class t1_heavy_classes[5]={{1, 1, 40000, 0, 0, 0}, 
								{2, 2, 20000, 0, 0, 0},
								{3, 3, 20000, 0, 0, 0},
								{4, 4, 30000, 0, 0, 0},
								{5, 5, 50000, 0, 0, 0}};
	
	index=test_fair_token_bucket(t1_heavy_classes, 5, 120000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class t2_heavy_classes[5]={{1, 1, 40000, 0, 0, 0}, 
								{2, 2, 20000, 0, 0, 0},
								{3, 3, 20000, 0, 0, 0},
								{4, 4, 30000, 0, 0, 0},
								{5, 10, 50000, 0, 0, 0}};
	
	index=test_fair_token_bucket(t2_heavy_classes, 5, 120000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class t3_heavy_classes[5]={{1, 1, 40000, 0, 0, 0}, 
								{2, 2, 20000, 0, 0, 0},
								{3, 3, 22000, 0, 0, 0},
								{4, 4, 20000, 0, 0, 0},
								{5, 10, 10000, 0, 0, 0}};
	
	index=test_fair_token_bucket(t3_heavy_classes, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class all_light_classes[5]={{1, 1, 20000, 0, 0, 0}, 
								{2, 2, 20000, 0, 0, 0},
								{3, 3, 22000, 0, 0, 0},
								{4, 4, 20000, 0, 0, 0},
								{5, 10, 23000, 0, 0, 0}};
	
	index=test_fair_token_bucket(all_light_classes, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class light_with_big_player[5]={{1, 1, 10000, 0, 0, 0}, 
								{2, 2, 10000, 0, 0, 0},
								{3, 3, 10000, 0, 0, 0},
								{4, 4, 10000, 0, 0, 0},
								{5, 10, 60000, 0, 0, 0}};
	
	index=test_fair_token_bucket(light_with_big_player, 5, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class sandvine_active_logic[10]={{1, 5, 30000, 0, 0, 0}, 
								{2, 5, 30000, 0, 0, 0},
								{3, 3, 12000, 0, 0, 0},
								{4, 3, 12000, 0, 0, 0},
								{5, 3, 11000, 0, 0, 0},
								{6, 2, 14000, 0, 0, 0},
								{7, 2, 5000, 0, 0, 0},
								{8, 2, 8000, 0, 0, 0},
								{9, 2, 9000, 0, 0, 0},
								{10, 2, 20000, 0, 0, 0}};
	
	index=test_fair_token_bucket(sandvine_active_logic, 10, 100000, 200000, duration_s, 1);
	EXPECT_NEAR(index, 1, 0.02);
}
TEST(FairTokenBucket, Weight5000)
{
	size_t n_class=5000;
	long long per_class_CIR=1000000;
	long long CIR=n_class*per_class_CIR;
	long long CBS=CIR;
	struct sftb_class very_heavy_classes[n_class];
	memset(very_heavy_classes, 0, sizeof(very_heavy_classes));
	for(size_t i=0; i<n_class; i++)
	{
		very_heavy_classes[i].class_id=i;
		very_heavy_classes[i].requested_CIR=per_class_CIR+(random()%20)*per_class_CIR;
		very_heavy_classes[i].weight=i%20+1;
	}
	double index=test_fair_token_bucket(very_heavy_classes, n_class, CIR, CBS, 40, 1);
	EXPECT_NEAR(index, 1, 0.02);

	struct sftb_class slight_heavy_classes[n_class];
	memset(slight_heavy_classes, 0, sizeof(slight_heavy_classes));
	for(size_t i=0; i<n_class; i++)
	{
		slight_heavy_classes[i].class_id=i;
		slight_heavy_classes[i].requested_CIR=per_class_CIR+i*per_class_CIR/n_class;
		slight_heavy_classes[i].weight=i%20+1;
	}
	index=test_fair_token_bucket(slight_heavy_classes, n_class, CIR, CBS, 40, 1);
	EXPECT_NEAR(index, 1, 0.02);
}

struct btb_key
{
	long long key;
	long long request_CIR;
	long long ideal_tokens;
	long long allocated_tokens;
};
void btb_sync(struct bulk_token_bucket **btb, size_t n_btb)
{
	char *blob=NULL;
	size_t blob_sz=0;
	for(size_t i=0; i<n_btb; i++)
	{
		for(size_t j=0; j<n_btb; j++)
		{
			if(i==j) continue;
			bulk_token_bucket_serialize(btb[j], &blob, &blob_sz);
			bulk_token_bucket_merge_blob(btb[i], blob, blob_sz);
			free(blob);
			blob=NULL;
		}
	}
	return;
}
struct btb_case
{
	long long CIR;
	long long CBS;
	int n_replica;
	int bucket_num;
	int key_num;
	int duration_s;
	
	long long estimate_keys;
	long long more;
	long long less;
	double collision_rate;
	double index;
};
void bulk_token_bucket_test_print_result(const struct btb_case *results, size_t n_result)
{
	printf("bkt\trepl\tkey\testk\tmore\tless\tcoll\tindex\n");
	for(size_t i=0; i<n_result; i++)
	{
		printf("%d\t%d\t%d\t%lld\t%lld\t%lld\t%.4f\t%.4f\n", results[i].bucket_num, results[i].n_replica,
						results[i].key_num, results[i].estimate_keys, results[i].more, results[i].less,
						results[i].collision_rate, results[i].index);
	} 
}
void bulk_token_bucket_test(struct btb_case *mycase)
{
	uuid_t uuid;
	uuid_generate(uuid);
	struct timeval start, step, now;
	int step_us=1000;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long max_tokens=mycase->duration_s*mycase->CIR+mycase->CBS;
	struct bulk_token_bucket *btb[mycase->n_replica];
	int n_replica=mycase->n_replica;
	int key_num=mycase->key_num;
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		btb[i]=bulk_token_bucket_new(uuid, now, mycase->CIR, mycase->CBS, mycase->bucket_num);
	}
	
	struct btb_key bk[key_num];
	for(int i=0; i<key_num; i++)
	{
		bk[i].key=random();
		bk[i].request_CIR=mycase->CIR/2+(i*mycase->CIR)/key_num;
		bk[i].allocated_tokens=0;
		if(bk[i].request_CIR<mycase->CIR)
		{
			bk[i].ideal_tokens=bk[i].request_CIR*mycase->duration_s;
		}
		else
		{
			bk[i].ideal_tokens=max_tokens;
		}
	}
	int sync_interval_ms=2000;
	for(int i=0; i<mycase->duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		for(int j=0; j<key_num; j++)
		{
			int r=random()%mycase->n_replica;
			bk[j].allocated_tokens+=bulk_token_bucket_consume(btb[r], (char *)&(bk[j].key), sizeof(bk[j].key),
					now, TB_CMD_CONSUME_FLEXIBLE,
					bk[j].request_CIR*step_us/(1000*1000));
		}
		if(0==i%(sync_interval_ms*1000/step_us))
		{
			btb_sync(btb, mycase->n_replica);
		}
	}
	double index=0, ratio=0;
	long long more=0, less=0;
	for(int i=0; i<key_num; i++)
	{
		index += pow((double)(bk[i].ideal_tokens-bk[i].allocated_tokens)/bk[i].ideal_tokens, 2);		
		ratio = (double)bk[i].allocated_tokens/bk[i].ideal_tokens;
		if(ratio > 1.02) more++;
		if (ratio <0.98) less++;
		if(ratio>1.02 || ratio < 0.98)
		{
			//printf("%f\n", ratio);
		}
	}
	index=1-sqrt(index/key_num);
	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb[0], &info);
	mycase->index=index;
	mycase->estimate_keys=info.estimate_keys;
	mycase->more=more;
	mycase->less=less;
	mycase->collision_rate=info.collision_rate;

	for(int i=0; i<n_replica; i++)
	{
		bulk_token_bucket_free(btb[i]);
	}
	return;
}
TEST(BulkTokenBucket, Basic)
{
	uuid_t uuid;
	uuid_generate(uuid);
	struct timeval start, step, now;
	int step_us=1000, duration_s=100;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long CIR=10000, CBS=10000;
	long long request_CIR=10000;
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, CIR, CBS, 4);
	
	long long got_tokens=0, max_tokens=duration_s*CIR+CBS;
	const char *key="192.168.0.1";

	int i=0;
	for(i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		got_tokens+=bulk_token_bucket_consume(btb, key, strlen(key), now, TB_CMD_CONSUME_FLEXIBLE, request_CIR*step_us/(1000*1000));
	}
	bulk_token_bucket_free(btb);
	EXPECT_LE(got_tokens, max_tokens);
}
TEST(BulkTokenBucket, RareCollision)
{
	int n_case=7;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=1000*1000;
		test[i].CBS=1000*1000;
		test[i].bucket_num=512;
		test[i].n_replica=1;
		test[i].key_num=(1<<i);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);

}
TEST(BulkTokenBucket, HighCollision)
{
	int n_case=4;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=1000*1000;
		test[i].CBS=1000*1000;
		test[i].bucket_num=512;
		test[i].n_replica=1;
		test[i].key_num=128*(i+1);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, VariousCIR)
{
	int n_case=5;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=400*1000*(i+1);
		test[i].CBS=1000*1000;
		test[i].bucket_num=512;
		test[i].n_replica=1;
		test[i].key_num=32;
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, Bucket1M)
{
	int n_case=2;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=1000*1000;
		test[i].CBS=1000*1000;
		test[i].bucket_num=1024*1024;
		test[i].n_replica=1;
		test[i].key_num=1024*(i+1);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, Replicas)
{
	int n_case=5;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=1000*1000;
		test[i].CBS=2*1000*1000;
		test[i].bucket_num=512;
		test[i].n_replica=2;
		test[i].key_num=1<<i;
		//test[i].key_num=16*(i+1);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, Replica4)
{
	int n_case=8;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].CIR=1000*1000;
		test[i].CBS=2*1000*1000;
		test[i].bucket_num=512;
		test[i].n_replica=4;
		test[i].key_num=1<<i;
		//test[i].key_num=16*(i+1);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}