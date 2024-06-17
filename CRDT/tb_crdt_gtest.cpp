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

long long get_request_tokens(int index, enum traffic_type type, long long step_us, long long CIR, long long CBS) 
{
	const int REPLICA_NUMBER=3;
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
	const int REPLICA_NUMBER=3;
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
		buckets[i] = OC_token_bucket_new(uuid, start, CIR, 1, CBS);
	}
	long long tokens = 0, flexible_tokens = 0;
	long long consumed = 0, requested = 0, upper_limit = 0;
	long long mimic_duration_us = (long long)100*1000*1000;
	long long step_us = 100;
	struct timeval step, now;
	now = start;
	step.tv_sec = 0;
	step.tv_usec = (suseconds_t)step_us;
	for (i = 0; (long long)i < mimic_duration_us / step_us; i++) 
	{
		j = i % 3; // sequence selection
		timeradd(&now, &step, &now);
		tokens = get_request_tokens(j, type, step_us, CIR, CBS);
		flexible_tokens = OC_token_bucket_consume(buckets[j], now, TB_CONSUME_FLEXIBLE, tokens);
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
	EXPECT_LE(consumed, requested);
	double accuracy = (double)consumed / MIN(upper_limit, requested);
	EXPECT_NEAR(accuracy, 1, 0.02);
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

	bucket=OC_token_bucket_new(uuid, now, CIR, 1, CBS);
	long long tokens=0;

	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 140);
	EXPECT_EQ(tokens, 140);
	
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 61);
	EXPECT_EQ(tokens, 0);

	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 60);
	EXPECT_EQ(tokens, 60);

	now.tv_sec++;
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 90);
	EXPECT_EQ(tokens, 90);

	struct OC_token_bucket_info info;
	OC_token_bucket_info(bucket, now, &info);
	EXPECT_GE(info.available, 10);
//	printf("avail=%lld\n", tokens);
	
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 10);
	EXPECT_EQ(tokens, 10);
	OC_token_bucket_free(bucket);
}
TEST(OCTokenBucket, Period)
{
	uuid_t uuid;
	uuid_generate(uuid);

	struct OC_token_bucket *bucket=NULL;
	long long rate=100;
	long long capacity=40;
	long long period=300;
	struct timeval now;
	gettimeofday(&now, NULL);

	bucket=OC_token_bucket_new(uuid, now, rate, period, capacity);
	long long tokens=0;

	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, capacity);
	EXPECT_EQ(tokens, capacity);

	now.tv_sec+=period/10;
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 1+rate/10);
	EXPECT_EQ(tokens, 0);

	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, rate/10);
	EXPECT_EQ(tokens, rate/10);
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
		b[i]=OC_token_bucket_new(uuid, now, CIR, 1, CBS);
		OC_token_bucket_consume(b[i], now, TB_CONSUME_NORMAL, 10);
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
	long long tokens=0;

	//Zero CIR
	bucket=OC_token_bucket_new(uuid, now, 0, 1, 1000);
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 1000);
	EXPECT_EQ(tokens, 1000);

	now.tv_sec++;
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 1);
	EXPECT_EQ(tokens, 0);
	OC_token_bucket_free(bucket);

	//Zero CBS
	bucket=OC_token_bucket_new(uuid, now, 1000, 1, 0);
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 1);
	EXPECT_EQ(tokens, 0);
	OC_token_bucket_free(bucket);

	//Infinite Tokens
	long long consumed=0;
	long long capacity=1;
	bucket=OC_token_bucket_new(uuid, now, 1, 0, capacity);
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 140);
	consumed+=tokens;
	EXPECT_EQ(tokens, 140);


	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 65535);
	consumed+=tokens;
	EXPECT_EQ(tokens, 65535);

	struct OC_token_bucket_info info;
	
	OC_token_bucket_info(bucket, now, &info);
	EXPECT_EQ(info.consumed, consumed);
	EXPECT_EQ(info.refilled, consumed+capacity);
	EXPECT_LE(info.refilled-info.consumed, info.capacity);
	
	now.tv_sec++;
	OC_token_bucket_configure(bucket, now, 100, 1, 500);
	
	now.tv_sec+=500/100;
	tokens=OC_token_bucket_consume(bucket, now, TB_CONSUME_NORMAL, 500);
	consumed+=tokens;
	EXPECT_EQ(tokens, 500);

	OC_token_bucket_info(bucket, now, &info);
	EXPECT_EQ(info.consumed, consumed);
	EXPECT_LE(info.refilled-info.consumed, info.capacity);
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
		buckets[i]=OC_token_bucket_new(uuid, now, CIR, 1, CBS);
	}
	long long tokens=0;
	OC_token_bucket_sync(buckets, 2);
	tokens=OC_token_bucket_consume(buckets[0], now, TB_CONSUME_NORMAL, 130);
	EXPECT_EQ(tokens, 130);
	tokens=OC_token_bucket_consume(buckets[1], now, TB_CONSUME_NORMAL, 30);
	EXPECT_EQ(tokens, 30);

	OC_token_bucket_sync(buckets, 2);
	now.tv_sec++;
	//available=200-130-30+100=140
	//reserved=CIR/2=50, so local avaliable is 90
	tokens=OC_token_bucket_consume(buckets[0], now, TB_CONSUME_NORMAL, 91);
	EXPECT_EQ(tokens, 0);
	tokens=OC_token_bucket_consume(buckets[1], now, TB_CONSUME_FLEXIBLE, 90);
	EXPECT_EQ(tokens, 90);
	OC_token_bucket_sync(buckets, 2);
	
	for(i=0; i<2; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
TEST(OCTokenBucket, Replicate)
{
	struct OC_token_bucket *buckets[2];
	uuid_t uuid;
	long long CIR=100;
	long long CBS=200;
	long long tokens=0;
	struct timeval now;
	gettimeofday(&now, NULL);
	uuid_generate(uuid);
	buckets[0]=OC_token_bucket_new(uuid, now, CIR, 1, CBS);
	tokens=OC_token_bucket_consume(buckets[0], now, TB_CONSUME_NORMAL, 130);
	EXPECT_EQ(tokens, 130);

	char *blob=NULL;
	size_t blob_sz;
	OC_token_bucket_serialize(buckets[0], &blob, &blob_sz);
	
	uuid_generate(uuid);
	buckets[1]=OC_token_bucket_replicate(uuid, blob, blob_sz);
	free(blob);
	blob=NULL;
	tokens=OC_token_bucket_consume(buckets[1], now, TB_CONSUME_NORMAL, 30);
	OC_token_bucket_sync(buckets, 2);

	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[1], now, &info);
	EXPECT_EQ(info.consumed, 160);
	EXPECT_EQ(info.number_of_replica, 2);
	
	for(int i=0; i<2; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
double test_oc_token_bucket(size_t replica_num, long long mimic_duration_s, long long sync_interval_ms, int saturation_percent)
{
	struct OC_token_bucket *buckets[replica_num];
	size_t i=0, j=0;
	long long period=8;
	long long rate=512*1024*period;
	long long capacity=2*1024*1024;

	long long CIR=rate/period;

	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<replica_num; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, rate, period, capacity);
	}
	srandom(17);
	long long tokens=0;
	long long allocated=0, requested=0, upper_limit=0, refilled=0, tmp=0;
	long long step_us=100, mimic_duration_us=mimic_duration_s*1000*1000;
	long long sync_interval_us=sync_interval_ms*1000;
	struct timeval step, now;
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);
		j=random()%replica_num;
		
		tokens=CIR*step_us/(1000*1000);
		tokens=tokens*saturation_percent/100;
		tokens=tokens/2+random()%(tokens+1);
		requested+=tokens;
		tmp=OC_token_bucket_consume(buckets[j], now, TB_CONSUME_NORMAL, tokens);
		allocated+=tmp;
		if((step_us*i)%sync_interval_us==0)
		{
			OC_token_bucket_sync(buckets, replica_num);
		}
		upper_limit=capacity+CIR*(timeval_to_ms(now)-timeval_to_ms(start))/1000;
	}
	

	struct OC_token_bucket_info info;
	OC_token_bucket_info(buckets[0], now, &info);
	refilled=info.refilled;
	
	upper_limit=capacity+CIR*timeval_delta_ms(start, now)/1000;
	
	printf("duration_s=%lld, replica_num=%zu, upper_limit=%lld, requested=%lld, allocated=%lld, wasted=%lld\n", 
	mimic_duration_s, replica_num, upper_limit, requested, allocated, (requested>upper_limit?upper_limit-allocated:0));
	double allowed_error=(double)1/replica_num/mimic_duration_s;
	EXPECT_NEAR((double)allocated/MIN(refilled, requested), 1, MAX(allowed_error, 0.03));
	EXPECT_LE(allocated, requested);
	double accuracy=(double)allocated/MIN(upper_limit, requested);
//	double real_saturation=(double)(requested)/(upper_limit);
//	EXPECT_NEAR(real_saturation, (double)saturation_percent/100, 0.1);
	
	for(i=0; i<replica_num; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
	return accuracy;
}
TEST(OCTokenBucket, ShortPeriodHeavyConsumer)
{
	double accuracy=0.0;
	long long replica_num=2, test_duration_s=10, sync_interval_ms=10;
	for(int i=0; i<20; i++)
	{
		test_duration_s=10*(i+1);
		accuracy=test_oc_token_bucket(replica_num, test_duration_s, sync_interval_ms, 200);
		EXPECT_NEAR(accuracy, 1, 0.05);
	}
}
TEST(OCTokenBucket, HeavyConsumer)
{
	double accuracy=0.0;
	long long replica_num=0, test_duration_s=200, sync_interval_ms=100;

	long long i=0, j=0;
	for(i=6; i<9; i++)
	{
		replica_num=i+8;
		for(j=0; j<10; j++)
		{
			sync_interval_ms=100*(j+1);
			accuracy=test_oc_token_bucket(replica_num, test_duration_s, sync_interval_ms, 200);
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
			sync_interval_ms=10*(j+1);
			accuracy=test_oc_token_bucket(replica_num, test_duration_s, sync_interval_ms, 90);
			EXPECT_NEAR(accuracy, 1, 0.03);
		}
	}
}
TEST(OCTokenBucket, ConcurrentHeavyConsumer)
{
	const int REPLICA_NUMBER=3;
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
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, 1, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)100*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	now = start;

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
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CONSUME_FLEXIBLE, tokens);
		}
		if(i%80==0)
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
TEST(OCTokenBucket, MixTypeConsumer)
{
	const int n_replica=2;
	struct OC_token_bucket *buckets[n_replica];
	size_t i=0, j=0;
	long long CIR=20*1024*1024;
	long long CBS=40*1024*1024;
	uuid_t uuid;
	struct timeval start;
	gettimeofday(&start, NULL);
	for(i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, 1, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)100*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);
		for(j=0; j<n_replica; j++)
		{
			tokens=CIR*step_us/(1000*1000);
			tokens=MAX(3*j*tokens/n_replica, 1);
			requested+=tokens;
			consumed+=OC_token_bucket_consume(buckets[j%n_replica], now, TB_CONSUME_FLEXIBLE, tokens);
		}
		if(i%80==0)
		{
			OC_token_bucket_sync(buckets, n_replica);
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
	for(i=0; i<n_replica; i++)
	{
		OC_token_bucket_free(buckets[i]);
	}
}
TEST(OCTokenBucket, Reconfigure)
{
	const int REPLICA_NUMBER=3;
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
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, 1, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)1000*1000*1000;
	long long step_us=100;
	struct timeval step, now;
	now = start;

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
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CONSUME_FLEXIBLE, tokens);
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

	start = now;
	CIR*=2;
	CBS*=5;
	requested=0;
	consumed=0;
	OC_token_bucket_configure(buckets[0], start, CIR, 1, CBS);
	for(i=0; (long long)i<mimic_duration_us/step_us; i++)
	{
		timeradd(&now, &step, &now);		
		tokens=CIR*step_us/(1000*1000);		
		tokens=tokens/REPLICA_NUMBER+random()%tokens;
		for(j=0; j<REPLICA_NUMBER; j++)
		{
			requested+=tokens;			
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CONSUME_FLEXIBLE, tokens);
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
	const int REPLICA_NUMBER=3;
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
		buckets[i]=OC_token_bucket_new(uuid, start, CIR, 1, CBS);
	}
	srandom(17);
	long long tokens=0;
	long long consumed=0, requested=0, upper_limit=0, refilled=0;
	long long mimic_duration_us=(long long)100*1000*1000;
	long long step_us=100;
	struct timeval step, now;

	now = start;
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
			consumed+=OC_token_bucket_consume(buckets[j], now, TB_CONSUME_FLEXIBLE, tokens);
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

struct ftb_class
{
	long long class_id;
	long long weight;
	long long rate;
	double next_demand;
	long long demanded_tokens;
	long long allocated_tokens;
	long long ideal_tokens;
};

int cmp_ftb_member(const void *a, const void *b)
{
	struct ftb_class *ra=(struct ftb_class*)a;
	struct ftb_class *rb=(struct ftb_class*)b;
	return (int)(ra->demanded_tokens-rb->demanded_tokens);
}
double max_min_fairness_index(long long available_tokens, struct ftb_class * classes, size_t n_class)
{
	qsort(classes, n_class, sizeof(struct ftb_class), cmp_ftb_member);
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
			long long my_share = classes[i].weight*share;
			if(classes[i].demanded_tokens == classes[i].ideal_tokens)
			{
				continue;
			}
			else if(classes[i].demanded_tokens - classes[i].ideal_tokens <= my_share)
			{
				left_tokens -= (classes[i].demanded_tokens - classes[i].ideal_tokens);
				classes[i].ideal_tokens=classes[i].demanded_tokens;
			}
			else
			{
				left_tokens -= my_share;
				classes[i].ideal_tokens += my_share;
				
			}
		}
		left_weight=0;
		n_satisfied=0;
		for(size_t i=0; i<n_class; i++)
		{
			if(classes[i].demanded_tokens == classes[i].ideal_tokens)
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
		fair_token_bucket_serialize(ftb[i], &blob, &blob_sz);
		for(size_t j=0; j<n_ftb; j++)
		{
			if(i==j) continue;
			fair_token_bucket_merge_blob(ftb[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}
	return;
}
struct ftb_case
{
	long long rate;
	long long period;
	long long capacity;
	long long perturb_interval_ms;
	long long divisor;
	long long sync_interval_ms;
	int print;
	int duration_s;
	int n_replica;
	int randomization;
};

void ftb_case_init(struct ftb_case *mycase)
{
	memset(mycase, 0, sizeof(struct ftb_case));
	mycase->n_replica=1;
	mycase->divisor=8192;
	mycase->perturb_interval_ms=10;
	mycase->period=1;
	mycase->print=1;
	mycase->sync_interval_ms=100;
}
double expected_fairness_index=0.04;
double test_fair_token_bucket(struct ftb_class *classes, size_t n_class, struct ftb_case *mycase)
{
	uuid_t uuid;
	srand(171);
	for(size_t i=0; i<n_class; i++)
	{
		classes[i].demanded_tokens=0;
		classes[i].allocated_tokens=0;
		classes[i].ideal_tokens=0;
		classes[i].next_demand=0;
	}
	struct timeval start, step, now;
	int step_us=100;
	gettimeofday(&start, NULL);
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	int n_replica=mycase->n_replica;
	struct fair_token_bucket *ftb[n_replica];
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		ftb[i]=fair_token_bucket_new(uuid, now, mycase->rate, mycase->period, mycase->capacity, mycase->divisor);
		fair_token_bucket_set_pertub_interval(ftb[i], mycase->perturb_interval_ms);
	}
	int sync_interval_ms=mycase->sync_interval_ms;
	long long got=0;
	int r=0;
	struct timeval last_sync=now;
	for(int i=0; i<mycase->duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		int k=random()%n_class;
		for(size_t j=0; j<n_class; j++)
		{
			int idx=(j+k)%n_class;
			if(mycase->randomization && idx==(int)(now.tv_sec%n_class))
			{
				continue;
			}
			r=idx%n_replica;// each class sticks to one replica.
			classes[idx].next_demand += (double)classes[idx].rate*step_us/(1000*1000);
			if(classes[idx].next_demand < 1)
			{
				continue;
			}
			long long this_demand = (long long)floor(classes[idx].next_demand);
			classes[idx].next_demand -= this_demand;
			assert(classes[idx].next_demand >= 0);
			got=fair_token_bucket_consume(ftb[r], now, 
								(const char*) &(classes[idx].class_id),
								sizeof(classes[idx].class_id),
								classes[idx].weight,
								TB_CONSUME_NORMAL,
								this_demand);
			classes[idx].allocated_tokens += got;
			classes[idx].demanded_tokens += this_demand;
		}
		if(timeval_delta_ms(last_sync, now) > mycase->sync_interval_ms)
		{
			ftb_sync(ftb, n_replica);
			last_sync=now;
			//r=i%n_replica;
		}
	}
	long long available_tokens = mycase->rate*mycase->duration_s/mycase->period + mycase->capacity;
	double index=max_min_fairness_index(available_tokens, classes, n_class);
	if(mycase->print || index < 0.9)
	{
		printf("class\tweight\tdemand\tallocated\tideal\r\n");
		for(size_t i=0; i<n_class; i++)
		{
			if((double)classes[i].allocated_tokens/classes[i].ideal_tokens< 1.1 &&
			(double) classes[i].allocated_tokens/classes[i].ideal_tokens > 0.9)
			{

			}
				//continue;
			printf("%lld\t%lld\t%lld\t%lld\t%lld\r\n", classes[i].class_id,
										classes[i].weight,
										classes[i].demanded_tokens/mycase->duration_s,
										classes[i].allocated_tokens/mycase->duration_s,
										classes[i].ideal_tokens/mycase->duration_s);
		}
		printf("Replica %d, Perturb %lld ms, sync interval %d ms, fairness index %f\r\n", n_replica, mycase->perturb_interval_ms, sync_interval_ms, index);
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
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=100000;
	mycase.capacity=10000;
	mycase.duration_s=40;
	mycase.perturb_interval_ms=10;

	struct ftb_class two_classes[2]={
								{.class_id=1, .weight=1, .rate=110000}, 
								{.class_id=2, .weight=4, .rate=100},};
	
	index=test_fair_token_bucket(two_classes, 2, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class heavy_2_of_2_classes[2]={
								{.class_id=1, .weight=1, .rate=120000}, 
								{.class_id=2, .weight=4, .rate=10000},};
	
	index=test_fair_token_bucket(heavy_2_of_2_classes, 2, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class heavy_1_of_5_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=20000},
								{.class_id=5, .weight=1, .rate=50000}};
	
	index=test_fair_token_bucket(heavy_1_of_5_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class all_light_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=20000},
								{.class_id=5, .weight=1, .rate=20000}};
	
	index=test_fair_token_bucket(all_light_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class heavy_2_of_5_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=50000},
								{.class_id=4, .weight=1, .rate=50000},
								{.class_id=5, .weight=1, .rate=20003}};

	
	index=test_fair_token_bucket(heavy_2_of_5_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class all_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=40000}, 
								{.class_id=2, .weight=1, .rate=40000},
								{.class_id=3, .weight=1, .rate=50000},
								{.class_id=4, .weight=1, .rate=60000},
								{.class_id=5, .weight=1, .rate=40000}};
	
	index=test_fair_token_bucket(all_heavy_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, Merge)
{
	uuid_t uuid;
	
	long long CIR=10*1024*1024, CBS=CIR;
	
	struct timeval start, step, now;
	int step_us=1000;
	gettimeofday(&start, NULL);
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	int n_replica=2;
	struct fair_token_bucket *ftb[n_replica];
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		ftb[i]=fair_token_bucket_new(uuid, now, CIR, CBS, 1, 8192);
	}

	long long got=0;
	int duration_s=20;
	for(int i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		for(int j=0; j<n_replica; j++)
		{
			timeradd(&now, &step, &now);
			got+=fair_token_bucket_consume(ftb[1], now, 
								(const char*) &(j),
								sizeof(j),
								i+1,
								TB_CONSUME_NORMAL,
								100);
		}
	}

	ftb_sync(ftb, n_replica);
	struct fair_token_bucket_info info[n_replica];
	memset(info, 0, sizeof(info));
	fair_token_bucket_info(ftb[0], now, &info[0]);
	fair_token_bucket_info(ftb[1], now, &info[1]);
	EXPECT_EQ(info[0].active_key_number, info[1].active_key_number);

	for(int i=0; i<n_replica; i++)
	{
		fair_token_bucket_free(ftb[i]);
	}
}
TEST(FairTokenBucket, Debug)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=100000;
	mycase.capacity=10000;
	mycase.duration_s=40;
	mycase.perturb_interval_ms=10;

	struct ftb_class heavy_1_of_5_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=20000},
								{.class_id=5, .weight=1, .rate=50000}};
	
	index=test_fair_token_bucket(heavy_1_of_5_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, Replicas)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=120000;
	mycase.capacity=200000;
	mycase.duration_s=350;
	mycase.n_replica=2;
	mycase.perturb_interval_ms=10;
	mycase.sync_interval_ms=10;
	struct ftb_class one_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=20000},
								{.class_id=5, .weight=1, .rate=50000}};

	
	index=test_fair_token_bucket(one_heavy_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class two_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=50000},
								{.class_id=4, .weight=1, .rate=50000},
								{.class_id=500, .weight=1, .rate=20003}};
	
	index=test_fair_token_bucket(two_heavy_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	long long CIR=10*1024*1024;
	mycase.rate=CIR;
	mycase.capacity=CIR;
	mycase.duration_s=100;
	mycase.n_replica=1;

	struct ftb_class class2[2]={
								{.class_id=1, .weight=1, .rate=CIR*3/2,},
								{.class_id=2, .weight=2, .rate=CIR*3/2},};
	index=test_fair_token_bucket(class2, 2, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

}
TEST(FairTokenBucket, TwoWeightedClass)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=8*1024*1024;
	mycase.capacity=mycase.rate;
	mycase.duration_s=500;
	mycase.n_replica=1;
	struct ftb_class test_class[2]={
								{.class_id=1, .weight=1, .rate=mycase.rate}, 
								{.class_id=2, .weight=3, .rate=mycase.rate}};

	
	index=test_fair_token_bucket(test_class, 2, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, Weight)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=120000;
	mycase.capacity=200000;
	mycase.duration_s=500;
	mycase.n_replica=1;
	struct ftb_class one_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=30000},
								{.class_id=5, .weight=1, .rate=50000}};

	
	double one_heavy_index=test_fair_token_bucket(one_heavy_classes, 5, &mycase);
	EXPECT_NEAR(one_heavy_index, 1, expected_fairness_index);

	struct ftb_class t1_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=40000}, 
								{.class_id=2, .weight=2, .rate=20000},
								{.class_id=3, .weight=3, .rate=20000},
								{.class_id=4, .weight=4, .rate=30000},
								{.class_id=5, .weight=5, .rate=50000}};

	
	double t1_heavy_index=test_fair_token_bucket(t1_heavy_classes, 5, &mycase);
	EXPECT_NEAR(t1_heavy_index, 1, expected_fairness_index);

	struct ftb_class t2_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=40000}, 
								{.class_id=2, .weight=2, .rate=20000},
								{.class_id=3, .weight=3, .rate=20000},
								{.class_id=4, .weight=4, .rate=30000},
								{.class_id=5, .weight=10, .rate=50000}};
	
	double t2_heavy_index=test_fair_token_bucket(t2_heavy_classes, 5, &mycase);
	EXPECT_NEAR(t2_heavy_index, 1, expected_fairness_index);

	struct ftb_class t3_heavy_classes[5]={
								{.class_id=1, .weight=1, .rate=40000}, 
								{.class_id=2, .weight=2, .rate=20000},
								{.class_id=3, .weight=3, .rate=22000},
								{.class_id=4, .weight=4, .rate=20000},
								{.class_id=5, .weight=10, .rate=10000}};
	
	double t3_heavy_index=test_fair_token_bucket(t3_heavy_classes, 5, &mycase);
	EXPECT_NEAR(t3_heavy_index, 1, expected_fairness_index);

	struct ftb_class all_light_classes[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=2, .rate=20000},
								{.class_id=3, .weight=3, .rate=22000},
								{.class_id=4, .weight=4, .rate=20000},
								{.class_id=5, .weight=10, .rate=23000}};
	
	
	index=test_fair_token_bucket(all_light_classes, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class light_with_big_player[5]={
								{.class_id=1, .weight=1, .rate=10000}, 
								{.class_id=2, .weight=2, .rate=10000},
								{.class_id=3, .weight=3, .rate=10000},
								{.class_id=4, .weight=4, .rate=10000},
								{.class_id=5, .weight=10, .rate=60000}};
	
	double all_light_index=test_fair_token_bucket(light_with_big_player, 5, &mycase);
	EXPECT_NEAR(all_light_index, 1, expected_fairness_index);

	struct ftb_class sd_al[10]={
								{.class_id=1, .weight=5, .rate=30000}, 
								{.class_id=2, .weight=5, .rate=30000},
								{.class_id=3, .weight=3, .rate=12000},
								{.class_id=4, .weight=3, .rate=12000},
								{.class_id=5, .weight=3, .rate=11000},
								{.class_id=6, .weight=2, .rate=14000},
								{.class_id=7, .weight=2, .rate=5000},
								{.class_id=8, .weight=2, .rate=8000},
								{.class_id=9, .weight=2, .rate=9000},
								{.class_id=10, .weight=2, .rate=20000}};
	
	double sd_al_index=test_fair_token_bucket(sd_al, 10, &mycase);
	EXPECT_NEAR(sd_al_index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, Weight5000)
{
	size_t n_class=5000;
	long long per_class_CIR=1000000;

	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=n_class*per_class_CIR;
	mycase.capacity=mycase.rate;
	mycase.duration_s=40;
	mycase.n_replica=1;
	mycase.perturb_interval_ms=10;

	struct ftb_class very_heavy_classes[n_class];
	memset(very_heavy_classes, 0, sizeof(very_heavy_classes));
	for(size_t i=0; i<n_class; i++)
	{
		very_heavy_classes[i].class_id=i;
		very_heavy_classes[i].rate=per_class_CIR+(random()%20)*per_class_CIR;
		very_heavy_classes[i].weight=i%20+1;
	}
	double index=test_fair_token_bucket(very_heavy_classes, n_class, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class slight_heavy_classes[n_class];
	memset(slight_heavy_classes, 0, sizeof(slight_heavy_classes));
	for(size_t i=0; i<n_class; i++)
	{
		slight_heavy_classes[i].class_id=i;
		slight_heavy_classes[i].rate=per_class_CIR+i*per_class_CIR/n_class;
		slight_heavy_classes[i].weight=i%20+1;
	}
	index=test_fair_token_bucket(slight_heavy_classes, n_class, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, VariousPerturb1)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=50000;
	mycase.capacity=50000;
	mycase.duration_s=20;
	mycase.print=0;
	struct ftb_class one_heavy_classes[2]={
								{.class_id=1, .weight=1, .rate=50000}, 
								{.class_id=2, .weight=4, .rate=100},};
	
	int max_replicas=8;
	long long perturb_array[6]={200, 150, 100, 50, 20, 10};
	printf("replica\tperturb\tindex\r\n");
	for(int i=0; i<max_replicas; i++)
	{
		for(size_t j=0; j<sizeof(perturb_array)/sizeof(long long); j++)
		{
			mycase.n_replica=i+1;
			mycase.perturb_interval_ms=perturb_array[j];
			index=test_fair_token_bucket(one_heavy_classes, 2, &mycase);
			//EXPECT_NEAR(index, 1, expected_fairness_index);
			printf("%d\t%lld\t%.2f\r\n", i+1, perturb_array[j], index);
		}
	}
}
TEST(FairTokenBucket, VariousPerturb2)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=50000;
	mycase.capacity=50000;
	mycase.duration_s=20;
	mycase.print=0;
	int max_replicas=3;
	long long perturb_array[7]={250, 200, 100, 50, 20, 10, 5};

	struct ftb_class two_heavy_classes[6]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2,  .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=50000},
								{.class_id=400, .weight=1, .rate=50000},
								{.class_id=5, .weight=1, .rate=20003},
								{.class_id=6, .weight=1, .rate=100}};
	printf("replica\tperturb\tindex\r\n");
	for(int i=0; i<max_replicas; i++)
	{
		for(size_t j=0; j<sizeof(perturb_array)/sizeof(long long); j++)
		{
			mycase.n_replica=i+1;
			mycase.perturb_interval_ms=perturb_array[j];
			index=test_fair_token_bucket(two_heavy_classes, sizeof(two_heavy_classes)/sizeof(struct ftb_class), &mycase);
			//EXPECT_NEAR(index, 1, expected_fairness_index);
			printf("%d\t%lld\t%.2f\r\n", i+1, perturb_array[j], index);
		}
	}
}
TEST(FairTokenBucket, Key100)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=50000;
	mycase.capacity=50000;
	mycase.duration_s=40;
	mycase.perturb_interval_ms=100;
	mycase.print=1;
	int n_key=100;
	struct ftb_class myclasses[n_key];
	for(int i=0; i<n_key; i++)
	{
		myclasses[i].class_id=i;
		myclasses[i].weight=1;
		myclasses[i].rate=100*(i+1);
	}
	index=test_fair_token_bucket(myclasses, n_key, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
TEST(FairTokenBucket, DynamicMembers)
{
	double index=0;
	struct ftb_case mycase;
	ftb_case_init(&mycase);
	mycase.rate=100000;
	mycase.capacity=10000;
	mycase.duration_s=40;
	mycase.perturb_interval_ms=10;
	mycase.randomization=1;
	struct ftb_class all_heavy_class[5]={
								{.class_id=1, .weight=1, .rate=40000}, 
								{.class_id=2, .weight=1, .rate=40000},
								{.class_id=3, .weight=1, .rate=50000},
								{.class_id=4, .weight=1, .rate=50000},
								{.class_id=5, .weight=1, .rate=40003}};

	
	index=test_fair_token_bucket(all_heavy_class, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);

	struct ftb_class heavy_1_of_5_class[5]={
								{.class_id=1, .weight=1, .rate=20000}, 
								{.class_id=2, .weight=1, .rate=20000},
								{.class_id=3, .weight=1, .rate=20000},
								{.class_id=4, .weight=1, .rate=30000},
								{.class_id=5, .weight=1, .rate=90003}};

	
	index=test_fair_token_bucket(heavy_1_of_5_class, 5, &mycase);
	EXPECT_NEAR(index, 1, expected_fairness_index);
}
struct btb_key
{
	long long key;
	long long request_CIR;
	long long ideal_tokens;
	long long allocated_tokens;
	long long requested_tokens;
	long long request_cnt;
};
void btb_sync(struct bulk_token_bucket **btb, size_t n_btb)
{
	char *blob=NULL;
	size_t blob_sz=0;
	if(n_btb==1) return;
	for(size_t i=0; i<n_btb; i++)
	{
		bulk_token_bucket_serialize(btb[i], &blob, &blob_sz);
		for(size_t j=0; j<n_btb; j++)
		{
			if(i==j) continue;
			
			bulk_token_bucket_merge_blob(btb[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}
	return;
}
struct btb_case
{
	long long rate;
	long long capacity;
	long long period;
	int bucket_num;
	int n_replica;
	long long n_key;

	int duration_s;
	
	long long more;
	long long less;
	long long kickouts;
	double index;
};
void bulk_token_bucket_test_print_result(const struct btb_case *results, size_t n_result)
{
	printf("bkt\trepl\tkey\tmore\tless\tindex\n");
	for(size_t i=0; i<n_result; i++)
	{
		printf("%d\t%d\t%lld\t%lld\t%lld\t%.4f\n", results[i].bucket_num, results[i].n_replica,
						results[i].n_key, results[i].more, results[i].less,
						results[i].index);
	} 
}
void bulk_token_bucket_test(struct btb_case *mycase)
{
	uuid_t uuid;
	struct timeval start, step, now;
	int step_us=1000;
	gettimeofday(&start, NULL);
	now=start;
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long max_tokens = mycase->duration_s*mycase->rate/mycase->period+mycase->capacity;
	struct bulk_token_bucket *btb[mycase->n_replica];
	int n_replica=mycase->n_replica;
	int n_key=mycase->n_key;
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		btb[i]=bulk_token_bucket_new(uuid, now, mycase->rate, mycase->period, mycase->capacity, mycase->bucket_num);
	}
	
	struct btb_key bk[n_key];
	memset(bk, 0, sizeof(bk));
	long long key_base = 2192381923;
	for(int i=0; i<n_key; i++)
	{
		bk[i].key = key_base+i;
		bk[i].request_CIR=mycase->rate/mycase->period/2+((i+1)*mycase->rate);
		bk[i].allocated_tokens=0;
		if(bk[i].request_CIR<mycase->rate/mycase->period)
		{
			bk[i].ideal_tokens=bk[i].request_CIR*mycase->duration_s;
		}
		else
		{
			bk[i].ideal_tokens=max_tokens;
		}
	}
	int sync_interval_ms=40;
	struct timeval last_sync=now;

	for(int i=0; i<mycase->duration_s*(1000*1000/step_us); i++)
	{
		
		for(int j=0; j<n_key; j++)
		{
			int r = random()%mycase->n_replica;
			int request = bk[j].request_CIR*step_us/(1000*1000);
			bk[j].allocated_tokens += bulk_token_bucket_consume(btb[r], now, (char *)&(bk[j].key), sizeof(bk[j].key),
					TB_CONSUME_FLEXIBLE,
					request);
			bk[j].requested_tokens += request;
			bk[j].request_cnt++;
		}
		if(timeval_delta_ms(last_sync, now) > sync_interval_ms)
		{
			btb_sync(btb, mycase->n_replica);
			last_sync=now;
		}
		timeradd(&now, &step, &now);
	}
	double index=0, ratio=0;
	long long more=0, less=0;
	for(int i=0; i<n_key; i++)
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
	index=1-sqrt(index/n_key);
	EXPECT_GE(index, 0.9);
	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb[0], now, &info);
	EXPECT_EQ(info.active_bucket_number, n_key);
	mycase->bucket_num=info.bucket_number;
	mycase->index=index;
	mycase->more=more;
	mycase->less=less;
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
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long CIR=10000, CBS=10000;
	long long request_CIR=10000;
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, CIR, 1, CBS, 4);
	

	long long allocated=0, max_tokens=duration_s*CIR+CBS, available=0;
	const char *key="192.168.0.1";
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, CBS);
	allocated+=bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, 100);
	EXPECT_EQ(allocated, 100);
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, CBS-allocated);

	int i=0;
	for(i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		allocated+=bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, request_CIR*step_us/(1000*1000));
	}
	bulk_token_bucket_free(btb);
	EXPECT_LE(allocated, max_tokens);
}
TEST(BulkTokenBucket, Period)
{
	uuid_t uuid;
	uuid_generate(uuid);

	long long rate=10, capacity=10, period=30;
	long long step_us=1000*1000, duration_s=period*100;
	long long request_CIR=1;

	struct timeval start, step, now;
	gettimeofday(&start, NULL);
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, rate, period, capacity, 4);

	long long allocated=0, max_tokens=duration_s/period*rate+capacity, available=0;
	const char *key="192.168.0.1";
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, capacity);
	allocated+=bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, capacity);
	EXPECT_EQ(allocated, capacity);
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, 0);

	int i=0;
	for(i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		timeradd(&now, &step, &now);
		allocated+=bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, request_CIR*step_us/(1000*1000));
	}
	EXPECT_EQ(allocated, max_tokens);
	now.tv_sec+=period/2;
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, capacity/2);
	bulk_token_bucket_free(btb);
}
TEST(BulkTokenBucket, Reconfigure)
{
	uuid_t uuid;
	uuid_generate(uuid);

	long long rate=10, capacity=10, period=30;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	now = start;

	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, rate, period, capacity, 4);

	long long allocated=0, available=0;
	const char *key="192.168.0.1";
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, capacity);
	allocated+=bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, capacity);
	EXPECT_EQ(allocated, capacity);
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, 0);

	now.tv_sec += 1;
	bulk_token_bucket_configure(btb, now, rate, period, capacity, 8);
	available=bulk_token_bucket_read_available(btb, now, key, strlen(key));
	EXPECT_EQ(available, capacity);
	
	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb, now, &info);
	EXPECT_EQ(info.bucket_number, 8);

	bulk_token_bucket_free(btb);
}
TEST(BulkTokenBucket, Merge)
{
	int n_replica=2;
	int n_key=2;
	struct bulk_token_bucket *btb[n_replica];
	long long allocated[n_key], requested[n_key];
	memset(allocated, 0, sizeof(allocated));
	memset(requested, 0, sizeof(requested));
	uuid_t uuid;
	struct timeval start, step, now, last_sync;
	int step_us=100*1000;
	gettimeofday(&start, NULL);
	last_sync=now=start;
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long CIR=8*1024, CBS=10*1024;
	char key[128];
	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		timeradd(&now, &step, &now);
		btb[i]=bulk_token_bucket_new(uuid, now, CIR, 1, CBS, 1<<(n_replica+1));
	}
	int r=0, k=0;
	int duration_s=100, sync_interval_ms=40;
	long long upper_limit=0;
	for(int i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		k = i%n_key;
		r = random()%n_replica;
		snprintf(key, sizeof(key), "192.168.0.%d", k);
		timeradd(&now, &step, &now);
		upper_limit=CIR*timeval_delta_ms(start, now)/1000+CBS;
		allocated[k] += bulk_token_bucket_consume(btb[r], now, key, strlen(key), TB_CONSUME_FLEXIBLE, 1024*4);
		assert(allocated[k] <= upper_limit);
		requested[k] += 1024*4;
		if(timeval_delta_ms(last_sync, now) > sync_interval_ms)
		{
			btb_sync(btb, n_replica);
			last_sync=now;
		}
	}
	upper_limit=CIR*timeval_delta_ms(start, now)/1000+CBS;
	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb[0], now, &info);
	EXPECT_EQ(info.active_bucket_number, n_key);
	EXPECT_EQ(info.replicas, n_replica);
	
	int success=0;
	for(int i=0; i<n_key; i++)
	{
		if((double)allocated[i]/upper_limit>0.95 && (double)allocated[i]/upper_limit <1.05) success++;
	}
	EXPECT_EQ(success, n_key);
	for(int i=0; i<n_replica; i++)
	{
		bulk_token_bucket_free(btb[i]);
	}
}

TEST(BulkTokenBucket, LowFillRate)
{
	int n_case=7;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].rate=1000*1000;
		test[i].capacity=1000*1000;
		test[i].period=1;
		test[i].bucket_num=512;
		test[i].n_replica=2;
		test[i].n_key=(1<<i);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);

}
TEST(BulkTokenBucket, Expansion)
{
	int n_case=4;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].rate=1000*1000;
		test[i].capacity=1000*1000;
		test[i].period=1;
		test[i].bucket_num=256;
		test[i].n_replica=2;
		test[i].n_key=128*(i+1);
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test+3, 1);
}
TEST(BulkTokenBucket, VariousRate)
{
	int n_case=5;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].rate=400*1000*(i+1);
		test[i].capacity=1000*1000;
		test[i].period=1;
		test[i].bucket_num=512;
		test[i].n_replica=2;
		test[i].n_key=32;
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
		test[i].rate=1000*1000;
		test[i].capacity=1000*1000;
		test[i].period=1;
		test[i].bucket_num=1024*1024;
		test[i].n_replica=1;
		test[i].n_key=1024*(i+1);
		test[i].duration_s=40;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, Replicas)
{
	int n_case=4;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].rate=1000*1000;
		test[i].capacity=2*1000*1000;
		test[i].period=1;
		test[i].bucket_num=64;
		test[i].n_replica=i+8;
		test[i].n_key=16; //1<<i;
		//test[i].n_key=16*(i+1);
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
		test[i].rate=1000*1000;
		test[i].capacity=2*1000*1000;
		test[i].period=1;
		test[i].bucket_num=512;
		test[i].n_replica=4;
		test[i].n_key=1<<i;
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, VariousKeys)
{
	int n_case=4;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].rate=1000*1000;
		test[i].capacity=2*1000*1000;
		test[i].period=1;
		test[i].bucket_num=256;
		test[i].n_replica=2;
		test[i].n_key=16<<i;
		test[i].duration_s=80;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, KeyNonStickyWithReplica10)
{
	int n_replica=2, n_key=64, n_bucket=512;
	struct bulk_token_bucket *btb[n_replica];
	long long allocated_tokens[n_key];
	char key[n_key][128];
	uuid_t uuid;
	struct timeval start, step, now;
	int step_us=100;
	gettimeofday(&start, NULL);
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long CIR=8*1024, CBS=10*1024;

	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		timeradd(&now, &step, &now);
		btb[i]=bulk_token_bucket_new(uuid, now, CIR, 1, CBS, n_bucket);
	}
	for(int i=0; i<n_key; i++)
	{
		snprintf(key[i], sizeof(key[i]), "192.168.0.%d", i+1);
		allocated_tokens[i]=0;
	}
	int r=0;
	int duration_s=40, sync_interval_ms=50;
	for(int i=0; i<duration_s*(1000*1000/step_us); i++)
	{
		r=i%n_replica;
		for(int j=0; j<n_key; j++)
		{
			allocated_tokens[j] += bulk_token_bucket_consume(btb[r], now, key[j], strlen(key[j]), TB_CONSUME_FLEXIBLE, 1024*4);
		}
		if(0==i%(sync_interval_ms*1000/step_us))
		{
			btb_sync(btb, n_replica);
		}
		timeradd(&now, &step, &now);
	}
	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb[0], now, &info);
	EXPECT_EQ(info.active_bucket_number, n_key);
	EXPECT_EQ(info.replicas, n_replica);
	long long upper_limit=CIR*timeval_delta_ms(start, now)/1000+CBS;
	int success=0, more=0, less=0;
	double index=0;
	for(int i=0; i<n_key; i++)
	{
		index += pow((double)(upper_limit - allocated_tokens[i])/upper_limit, 2);
		if((double)allocated_tokens[i]/upper_limit<0.95)
			less++;
		else if((double)allocated_tokens[i]/upper_limit > 1.05)
			more++;
		else
			success++;
	}
	index=1-sqrt(index/n_key);
	EXPECT_GE(index, 0.95);
	EXPECT_EQ(success, n_key);
	EXPECT_EQ(less, 0);
	EXPECT_EQ(more, 0);
	for(int i=0; i<n_replica; i++)
	{
		bulk_token_bucket_free(btb[i]);
	}
}
TEST(BulkTokenBucket, VariousPeriod)
{
	int n_case=8;
	struct btb_case test[n_case];
	for(int i=0; i<n_case; i++)
	{
		test[i].period=i+1;
		test[i].rate=256*1024*test[i].period;
		test[i].capacity=1024*1024*test[i].period;
		test[i].bucket_num=512;
		test[i].n_replica=2;
		test[i].n_key=1<<i;
		test[i].duration_s=100;
		bulk_token_bucket_test(test+i);
	}
	bulk_token_bucket_test_print_result(test, n_case);
}
TEST(BulkTokenBucket, MicroBenchmark)
{
	long long capacity=1024*1024*5;
	long long rate=capacity;
	long long period=1;
	long long duration_s=100;
	long long bucket_num=1024;
	const char *key="192.168.0.1";

	uuid_t uuid;
	struct timeval start, step, now, last_compare;
	int step_us=1*1000;
	gettimeofday(&start, NULL);
	last_compare=now=start;
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	long long max_tokens=duration_s*rate/period+capacity;
	long long allocated=0;
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, rate, period, capacity, bucket_num);
	while(timeval_delta_ms(start, now)<duration_s*1000)
	{
		allocated += bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, 1024*1024);
		timeradd(&now, &step, &now);
		if(timeval_delta_ms(last_compare, now)>100)
		{
			last_compare=now;
			long long max_tokens = timeval_delta_ms(start, now)*rate/period/1000+capacity;
			if(allocated>max_tokens)
			{
				printf("allocated = %lld, max_tokens = %lld, elapse = %ld \r\n", allocated, max_tokens, timeval_delta_ms(start, now)/1000);
			}
			EXPECT_LE(allocated, max_tokens);
		}
	}
	EXPECT_LE(allocated, max_tokens);
	bulk_token_bucket_free(btb);
}
TEST(BulkTokenBucket, RetireBucket)
{
	long long capacity=1024*1024*5;
	long long rate=capacity;
	long long period=1;
	long long duration_s=10;
	long long bucket_num=128;
	long long n_key=128;

	uuid_t uuid;
	struct timeval start, step, now, last_compare;
	int step_us=1*1000;
	gettimeofday(&start, NULL);
	last_compare=now=start;
	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;
	uuid_generate(uuid);
	struct bulk_token_bucket *btb=bulk_token_bucket_new(uuid, now, rate, period, capacity, bucket_num);
	while(timeval_delta_ms(start, now)<duration_s*1000)
	{
		for(int i=0; i<n_key; i++)
		{
			char key[128];
			snprintf(key, sizeof(key), "192.168.0.%d", i);
			bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, 1024*1024);
		}
		timeradd(&now, &step, &now);
	}

	struct bulk_token_bucket_info info;
	bulk_token_bucket_info(btb, now, &info);
	EXPECT_EQ(info.active_bucket_number, n_key);
	long long expanded_bucket_num = info.bucket_number;
	start = now;
	start.tv_sec += capacity/rate+1;
	now = start;
	while(timeval_delta_ms(start, now)<duration_s*1000)
	{
		for(int i=0; i<n_key; i++)
		{
			char key[128];
			snprintf(key, sizeof(key), "10.0.0.%d", i);
			bulk_token_bucket_consume(btb, now, key, strlen(key), TB_CONSUME_FLEXIBLE, 1024*1024);
		}
		timeradd(&now, &step, &now);
	}
	
	bulk_token_bucket_info(btb, now, &info);
	EXPECT_EQ(info.active_bucket_number, n_key);
	EXPECT_EQ(info.bucket_number, expanded_bucket_num);
	bulk_token_bucket_free(btb);
}
TEST(BulkTokenBucket, DifferentQuotaAlongReplicas)
{
	int n_replica=3, n_key=14, n_bucket=4;
	struct bulk_token_bucket *btb[n_replica];
	long long allocated_tokens[n_key];
	char key[n_key][128];
	uuid_t uuid;
	struct timeval start, step, now;
	int duration_s = 100;

	long long CIR=8*1024, CBS=10*1024;
	int step_us=100*1000, sync_interval_ms=100;
	long long small_consume = CIR*10/1000; //10ms refill interval
	gettimeofday(&start, NULL);
	now = start;

	step.tv_sec=0;
	step.tv_usec=(suseconds_t)step_us;

	for(int i=0; i<n_replica; i++)
	{
		uuid_generate(uuid);
		timeradd(&now, &step, &now);
		btb[i]=bulk_token_bucket_new(uuid, now, CIR, 1, CBS, n_bucket);
	}
	for(int i=0; i<n_key; i++)
	{
		snprintf(key[i], sizeof(key[i]), "10.10.1.%d", i+1);
		allocated_tokens[i]=0;
	}
	while(timeval_delta_ms(start, now)<duration_s*1000)
	{
		for(int i=0; i<n_key; i++)
		{
			int r = random()%(n_replica-1) + 1;
			allocated_tokens[i] += bulk_token_bucket_consume(btb[r], now, key[i], strlen(key[i]), TB_CONSUME_FLEXIBLE, 4*1024);
		}
		if(0==timeval_delta_ms(start, now)%sync_interval_ms)
		{
			btb_sync(btb, n_replica);
			for(int i=0; i<n_key; i++)
			{
				allocated_tokens[i] += bulk_token_bucket_consume(btb[0], now, key[i], strlen(key[i]), TB_CONSUME_FLEXIBLE, small_consume);
			}
		}
		timeradd(&now, &step, &now);
	}
	long long upper_limit=CIR*timeval_delta_ms(start, now)/1000+CBS;
	for(int i=0; i<n_key; i++)
	{
		EXPECT_LE(allocated_tokens[i], upper_limit);
	}
	for(int i=0; i<n_replica; i++)
	{
		bulk_token_bucket_free(btb[i]);
	}
}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}