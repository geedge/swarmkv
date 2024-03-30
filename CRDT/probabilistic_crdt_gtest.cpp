#include "crdt_utils.h"
#include "cm_sketch.h"
#include "st_hyperloglog.h"
#include "ap_bloom.h"

#include <gtest/gtest.h>
#include <unistd.h> //usleep
#include <uuid/uuid.h>
#include <math.h>
#define	CMS_TEST_WIDTH		(1024*16)
#define	CMS_TEST_DEPTH		6
TEST(CMSketch, Basic)
{
	uuid_t uuid;
	uuid_generate(uuid);
	struct CM_sketch *cms=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	int ret=0, n_added=0;
	for(int i=0; i<10; i++)
	{
		ret=CM_sketch_incrby(cms, (char *)&i, sizeof(i), i+1);
		n_added += i+1;
	}
	for(int i=0; i<10; i++)
	{
		ret=CM_sketch_query(cms, (char *)&i, sizeof(i));
		EXPECT_EQ(ret, i+1);
	}
	for(int i=0; i<10; i++)
	{
		ret=CM_sketch_incrby(cms, (char *)&i, sizeof(i), (0-i));
		n_added -= i;
	}
	for(int i=0; i<10; i++)
	{
		ret=CM_sketch_query(cms, (char *)&i, sizeof(i));
		//EXPECT_EQ(ret, 1);
	}
	struct CM_sketch_info info;
	CM_sketch_info(cms, &info);
	EXPECT_EQ(info.count, n_added);
	printf("error_rate: %f probability: %f\n", info.error, info.probability);
	CM_sketch_free(cms);
}
TEST(CMSketch, DimByProb)
{
	uuid_t uuid;
	uuid_generate(uuid);
	int width=0, depth=0;
	double error = 0.002, prob=0.001;
	CM_sketch_tool_dimension_by_prob(error, prob, &width, &depth);
	struct CM_sketch *cms=CM_sketch_new(uuid, width, depth);
	struct CM_sketch_info info;
	CM_sketch_info(cms, &info);
	EXPECT_NEAR(info.error, error, error/2);
	EXPECT_NEAR(info.probability, prob, prob/2);
	CM_sketch_free(cms);
}
TEST(CMSketch, I5K)
{
	uuid_t uuid;
	uuid_generate(uuid);
	struct CM_sketch *cms=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	long long n_item=5000, ret=0;
	long long base=10000, total_add=0;
	for(long long i=0; i<n_item; i++)
	{
		ret=CM_sketch_incrby(cms, (char *)&i, sizeof(i), base+i);
		total_add+=(base+i);
	}
	struct CM_sketch_info info;
	CM_sketch_info(cms, &info);
	long long pass=0;
	for(long long i=0; i<n_item; i++)
	{
		ret=CM_sketch_query(cms, (char *)&i, sizeof(i));
		//The formal expectation is total_add*info.error_rate
		if(abs(ret-(i+base))< (i+base)*info.error)
		{
			pass++;
		}
	}
	EXPECT_NEAR(pass, n_item, n_item*(1-info.probability));
	CM_sketch_free(cms);
}
static void CMS_sync(struct CM_sketch *list[], size_t n)
{
	char *blob=NULL;
	size_t blob_sz=0;
	
	for(size_t i=0; i<n; i++)
	{
		CM_sketch_serialize(list[i], &blob, &blob_sz);
		for(size_t j=0; j<n; j++)
		{
			if(j==i) continue;
			CM_sketch_merge_blob(list[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	return;
}
TEST(CMSketch, Merge)
{
	int replica_number=3, round=10;
	int n_item=4;
	const char *items[n_item]={"hello", "world", "good", "morning"};
	int count[n_item]={400, 300, 200, 100001};
	struct CM_sketch *cms[replica_number];
	uuid_t uuid;
	uuid_generate(uuid);
	cms[0]=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	char *blob=NULL;
	size_t blob_sz=0;
	CM_sketch_serialize(cms[0], &blob, &blob_sz);
	for(int i=1; i<replica_number; i++)
	{
		uuid_generate(uuid);
		cms[i]=CM_sketch_replicate(uuid, blob, blob_sz);
	}
	free(blob);
	blob=NULL;
	for(int i=0; i<n_item; i++)
	{
		CM_sketch_incrby(cms[i%replica_number], items[i], strlen(items[i]), count[i]);
	}
	CMS_sync(cms, replica_number);
	int ret=0;
	for(int i=0; i<round; i++)
	{
		int id=i%n_item;
		ret=CM_sketch_query(cms[i%replica_number], items[id], strlen(items[id]));
		EXPECT_EQ(ret, count[id]);
	}
	for(int i=0; i<n_item; i++)
	{
		CM_sketch_incrby(cms[(i+1)%replica_number], items[i], strlen(items[i]), 0-count[i]);
	}
	CMS_sync(cms, replica_number);
	for(int i=0; i<n_item; i++)
	{
		ret=CM_sketch_query(cms[i%replica_number], items[i], strlen(items[i]));
		EXPECT_EQ(ret, 0);
	}
	for(int i=0; i<replica_number; i++)
	{
		CM_sketch_free(cms[i]);
	}	
}
TEST(CMSketch, Idempotent)
{
	size_t replica_number=8, round=10000, i=0;
	struct CM_sketch *cms[replica_number];
	uuid_t uuid;
	
	for(i=0; i<replica_number; i++)
	{
		uuid_generate(uuid);
		cms[i]=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	}
	int n_added=0;
	for(i=0; i<round; i++)
	{
		CM_sketch_incrby(cms[i%replica_number], (char*) &i, sizeof(i), i);
		n_added+=i;
	}
	CMS_sync(cms, replica_number);
	CMS_sync(cms, replica_number);
	int ret=0;
	size_t success=0;
	struct CM_sketch_info info;
	CM_sketch_info(cms[0], &info);

	for(i=0; i<round; i++)
	{
		ret=CM_sketch_query(cms[(i+1)%replica_number], (char*) &i, sizeof(i));
		if((double)ret<((double)i+info.error*n_added))
		{
			success++;
		}
	}
	EXPECT_GE((double)success/round, info.probability);
	for(i=0; i<replica_number; i++)
	{
		CM_sketch_free(cms[i]);
	}	
}
TEST(CMSketch, Decrement)
{
	size_t replica_number=8, round=10000, i=0;
	struct CM_sketch *cms[replica_number];
	uuid_t uuid;
	
	for(i=0; i<replica_number; i++)
	{
		uuid_generate(uuid);
		cms[i]=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	}
	int n_added=0;
	for(i=0; i<round; i++)
	{
		CM_sketch_incrby(cms[i%replica_number], (char*) &i, sizeof(i), i*2);
		n_added+=i*2;
	}
	CMS_sync(cms, replica_number);
	int ret=0;
	size_t success=0;
	struct CM_sketch_info info;
	CM_sketch_info(cms[0], &info);
	EXPECT_EQ(info.count, n_added);
	for(i=0; i<round; i++)
	{
		CM_sketch_incrby(cms[(i+1)%replica_number], (char*) &i, sizeof(i), 0-i);
		n_added -= i;
	}
	CMS_sync(cms, replica_number);
	CM_sketch_info(cms[0], &info);
	EXPECT_EQ(info.count, n_added);
	for(i=0; i<round; i++)
	{
		ret=CM_sketch_query(cms[(i+1)%replica_number], (char*) &i, sizeof(i));
		if((double)ret<((double)i+info.error*n_added))
		{
			success++;
		}
	}
	EXPECT_GE((double)success/round, 1-info.probability);
	for(i=0; i<replica_number; i++)
	{
		CM_sketch_free(cms[i]);
	}

}
TEST(CMSketch, Accuracy)
{
	size_t replica_number=8, round=10000, i=0;
	struct CM_sketch *cms[replica_number];
	uuid_t uuid;
	
	for(i=0; i<replica_number; i++)
	{
		uuid_generate(uuid);
		cms[i]=CM_sketch_new(uuid, CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	}
	int n_added=0;
	for(i=0; i<round; i++)
	{
		CM_sketch_incrby(cms[i%replica_number], (char*) &i, sizeof(i), i);
		n_added+=i;
	}
	CMS_sync(cms, replica_number);
	int ret=0;
	size_t success=0;
	struct CM_sketch_info info;
	CM_sketch_info(cms[0], &info);
	EXPECT_EQ(info.count, n_added);
	for(i=0; i<round; i++)
	{
		ret=CM_sketch_query(cms[(i+1)%replica_number], (char*) &i, sizeof(i));
		if((double)ret<((double)i+info.error*n_added))
		{
			success++;
		}
	}
	EXPECT_GE((double)success/round, 1-info.probability);
	for(i=0; i<replica_number; i++)
	{
		CM_sketch_free(cms[i]);
	}
}
TEST(CMSketch, ResetReplica)
{
	size_t replica_number=2, round=10000, i=0;
	struct CM_sketch *cms[replica_number];
	int added[replica_number]={0};
	uuid_t uuid[replica_number];
	
	for(i=0; i<replica_number; i++)
	{
		uuid_generate(uuid[i]);
		cms[i]=CM_sketch_new(uuid[i], CMS_TEST_WIDTH, CMS_TEST_DEPTH);
	}

	int n_added=0;
	for(i=0; i<round; i++)
	{
		CM_sketch_incrby(cms[i%replica_number], (char*) &i, sizeof(i), i);
		added[i%replica_number]+=i;
		n_added+=i;
	}
	CMS_sync(cms, replica_number);
	struct CM_sketch_info info;
	CM_sketch_info(cms[0], &info);
	EXPECT_EQ(info.count, n_added);
	EXPECT_EQ(info.n_replica, replica_number);

	uuid_t replica_uuid[replica_number];
	size_t n_replica=replica_number;
	CM_sketch_list_replica(cms[0], replica_uuid, &n_replica);
	EXPECT_EQ(n_replica, replica_number);
	int match=0;
	for(i=0; i<replica_number; i++)
	{
		for(int j=0; j<(int)replica_number; j++)
		{
			if(0==uuid_compare(replica_uuid[i], uuid[j]))
			{
				match++;
				continue;
			}
		}
	}
	EXPECT_EQ(match, replica_number);

	int ret=0;
	srand(171);
	i=rand()%round;
	ret=CM_sketch_query(cms[0], (char*) &i, sizeof(i));
	EXPECT_NEAR(ret, i, info.error*n_added);
	CM_sketch_clear_replica(cms[0], uuid[i%replica_number]);
	ret=CM_sketch_query(cms[0], (char*) &i, sizeof(i));
	EXPECT_EQ(ret, 0);

	for(i=0; i<replica_number; i++)
	{
		CM_sketch_free(cms[i]);
	}

}
void st_hll_sync(struct ST_hyperloglog *list[], size_t n)
{
	char *blob=NULL;
	size_t blob_sz=0;
	if(n<2) return;
	for(size_t i=0; i<n; i++)
	{
		ST_hyperloglog_serialize(list[i], &blob, &blob_sz);
		for(size_t j=0; j<n; j++)
		{
			if(j==i) continue;
			ST_hyperloglog_merge_blob(list[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	return;
}
long long st_hll_test(unsigned char precision, int actual_count)
{
	int count=0;
	int key=random();
	struct timeval nouse;
	struct ST_hyperloglog *h=ST_hyperloglog_new(precision, 0, nouse);
	for(int i=0; i<actual_count; i++)
	{
		key++;
		ST_hyperloglog_add(h, (const char *)&key, sizeof(key), nouse);
	}
	count=ST_hyperloglog_count(h, nouse);
	ST_hyperloglog_free(h);
	return count;
}

TEST(STHyperLogLog, Basic)
{
	double error;
	long long est_count=0;
	long long count=10000;
	unsigned char precision=6;


	est_count=st_hll_test(precision, count);
	error=ST_hyperloglog_error_for_precision(precision);
	EXPECT_NEAR(est_count, count, error*count);

	precision=9;
	count=10000;
	est_count=st_hll_test(precision, count);
	error=ST_hyperloglog_error_for_precision(precision);
	EXPECT_NEAR(est_count, count, error*count);

}
TEST(STHyperLogLog, Duplicate)
{
	struct ST_hyperloglog *h1=NULL, *h2=NULL;
	struct timeval start, now, step;
	gettimeofday(&start, NULL);
	long long time_window_ms=1000;
	h1=ST_hyperloglog_new(9, time_window_ms, start);
	int n_add=0;
	now=start;
	step.tv_sec=0;
	step.tv_usec=1000;
	while(timeval_delta_ms(start, now)<time_window_ms*10)
	{
		ST_hyperloglog_add(h1, (const char *)&n_add, sizeof(n_add), now);
		n_add++;
		timeradd(&now, &step, &now);
	}
	double est1, est2;
	est1=ST_hyperloglog_count(h1, now);
	
	h2=ST_hyperloglog_duplicate(h1);
	est2=ST_hyperloglog_count(h2, now);
	EXPECT_EQ(est1, est2);

	ST_hyperloglog_merge(h2, h1);
	est2=ST_hyperloglog_count(h2, now);
	EXPECT_EQ(est1, est2);

	ST_hyperloglog_free(h1);
	ST_hyperloglog_free(h2);
}
struct st_hll_case
{
	unsigned char precision;
	long long time_window_ms;
	long long ideal_count;
	long long est_count;
	long long sync_interval_ms;
	int n_replica;
};
void st_hll_case_init(struct st_hll_case *mycase)
{
	memset(mycase, 0, sizeof(struct st_hll_case));
	mycase->precision=9;
	mycase->time_window_ms=5*1000;
	mycase->ideal_count=10000;
	mycase->n_replica=1;
	mycase->sync_interval_ms=10;
	mycase->est_count=0;
}
int st_hll_case_print(const struct st_hll_case *st_case, int n_case)
{
	int success=0;
	double real_error=0, est_error=0;
	printf("prcs\twin\tideal\test\test_err\treal_err\n");
	for(int j=0; j<n_case; j++)
	{
		est_error=ST_hyperloglog_error_for_precision(st_case[j].precision);
		real_error=(double)abs(st_case[j].est_count-st_case[j].ideal_count)/st_case[j].ideal_count;
		printf("%d\t%lld\t%lld\t%lld\t%f\t%f\n", st_case[j].precision,
										st_case[j].time_window_ms,
										st_case[j].ideal_count,
										st_case[j].est_count,
										est_error,
										real_error);
		if(real_error <= MAX(0.1, est_error))
		{
			success++;
		}
	}
	return success;
}
long long st_hll_test(struct st_hll_case *mycase)
{
	unsigned char precision=mycase->precision;
	int n_replica=mycase->n_replica;

	int count=0, per_step_count=0;
	mycase->sync_interval_ms=MAX(1, mycase->sync_interval_ms);
	mycase->n_replica=MAX(1, mycase->n_replica);
	struct timeval start, step, now;
	struct timeval last_sync;
	gettimeofday(&start, NULL);
	now=start;
	last_sync=start;

	struct ST_hyperloglog *h[n_replica];
	for(int i=0; i<n_replica; i++)
	{
		h[i]=ST_hyperloglog_new(precision, mycase->time_window_ms, start);
	}

	int key=1319823, j=0;
	int n_add=0;
	
	long long fixed_time_window_ms=10*1000;
	long long test_duration_ms=mycase->time_window_ms?(mycase->time_window_ms*10):fixed_time_window_ms;
	double per_second_count=(double)mycase->ideal_count*1000/(mycase->time_window_ms?mycase->time_window_ms:test_duration_ms);
	if(per_second_count>1000*1000)
	{
		step.tv_sec=0;
		step.tv_usec=1;
		per_step_count=per_second_count/1000/1000;
	}
	else
	{
		step.tv_sec=0;
		step.tv_usec=(suseconds_t)1000*1000/per_second_count;
		per_step_count=1;
	}
	while(timeval_delta_ms(start, now) < test_duration_ms)
	{
		for(int i=0; i<per_step_count; i++)
		{
			j=random()%n_replica;
			key++;
			ST_hyperloglog_add(h[j], (const char *)&key, sizeof(key), now);
			n_add++;
		}
		if(timeval_delta_ms(last_sync, now)>mycase->sync_interval_ms)
		{
			st_hll_sync(h, n_replica);
			last_sync=now;
			//double est=ST_hyperloglog_count(h[0], now);
			//printf("t+%d, idea: %.3f estimate: %.3f\n", (int)(now.tv_sec-start.tv_sec), per_second_count*time_window_s, est);
		}
		timeradd(&now, &step, &now);
	}


	st_hll_sync(h, n_replica);
	count=ST_hyperloglog_count(h[0], now);

	for(int i=0; i<n_replica; i++)
	{
		ST_hyperloglog_free(h[i]);
	}
	return count;
}
TEST(STHyperLogLog, NoSliding)
{
	struct st_hll_case st_case[128];
	int i=0;
	for(int j=6; j<19; j++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=j;
		st_case[i].time_window_ms=0;
		st_case[i].ideal_count=100*100;
		st_case[i].n_replica=1;
		st_case[i].est_count=st_hll_test(st_case+i);
		i++;
	}
	int success=st_hll_case_print(st_case, i);
	//allow 2 case failed.
	EXPECT_GE(success+2, i);
}
TEST(STHyperLogLog, VariousPrecision)
{
	int n_case=HLL_MAX_PRECISION-HLL_MIN_PRECISION+1;
	struct st_hll_case st_case[n_case];
	for(int i=0; i<n_case; i++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=HLL_MIN_PRECISION+i;
		st_case[i].time_window_ms=5*1000;
		st_case[i].ideal_count=70000;
		st_case[i].n_replica=1;
		st_case[i].sync_interval_ms=10;
		st_case[i].est_count=st_hll_test(st_case+i);
	}
	int success=st_hll_case_print(st_case, n_case);
	EXPECT_GE(success+4, n_case);

}
TEST(STHyperLogLog, VariousCount)
{
	int n_case=15;
	struct st_hll_case st_case[n_case];
	int i=0;
	for(i=0; i<n_case; i++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=15;
		st_case[i].time_window_ms=300*1000;
		st_case[i].ideal_count=100*(2<<i);
		st_case[i].n_replica=1;
		st_case[i].est_count=st_hll_test(st_case+i);		
	}
	int success=st_hll_case_print(st_case, n_case);
	EXPECT_GE(success+3, n_case);
}
TEST(STHyperLogLog, VariousWindow)
{
	int n_case=10;
	struct st_hll_case st_case[n_case];
	int i=0;
	for(i=0; i<n_case; i++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=15;
		st_case[i].time_window_ms=5*1000*(i*i+1);
		st_case[i].ideal_count=10000;
		st_case[i].n_replica=1;
		st_case[i].est_count=st_hll_test(st_case+i);		
	}
	int success=st_hll_case_print(st_case, n_case);
	EXPECT_EQ(success, n_case);
}
TEST(STHyperLogLog, VariousWindow70k)
{
	int n_case=10;
	struct st_hll_case st_case[n_case];
	int i=0;
	for(i=0; i<n_case; i++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=15;
		st_case[i].time_window_ms=10*(256<<i);
		st_case[i].ideal_count=70000;
		st_case[i].n_replica=1;
		st_case[i].est_count=st_hll_test(st_case+i);		
	}
	int success=st_hll_case_print(st_case, n_case);
	//70000 using linear estimation, which is the worst case for precision 15.
	//Much better at precision 9.
	//EXPECT_EQ(success, n_case);
	for(i=0; i<n_case; i++)
	{
		st_case[i].precision=9;
		st_case[i].time_window_ms=10*(256<<i);
		st_case[i].ideal_count=70000;
		st_case[i].n_replica=1;
		st_case[i].est_count=st_hll_test(st_case+i);		
	}
	success=st_hll_case_print(st_case, n_case);
	EXPECT_EQ(success, n_case);
}
TEST(STHyperLogLog, LinearCounting)
{
	struct st_hll_case st_case[64];
	int i=0;

	st_hll_case_init(st_case+i);
	st_case[i].precision=15;
	st_case[i].time_window_ms=0;
	st_case[i].ideal_count=70000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);		
	i++;

	st_hll_case_init(st_case+i);
	st_case[i].precision=15;
	st_case[i].time_window_ms=300;
	st_case[i].ideal_count=70000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);
	i++;

	st_hll_case_init(st_case+i);
	st_case[i].precision=14;
	st_case[i].time_window_ms=0;
	st_case[i].ideal_count=40000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);		
	i++;

	st_hll_case_init(st_case+i);
	st_case[i].precision=14;
	st_case[i].time_window_ms=300;
	st_case[i].ideal_count=40000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);
	i++;

	st_hll_case_init(st_case+i);
	st_case[i].precision=15;
	st_case[i].time_window_ms=300;
	st_case[i].ideal_count=10000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);
	i++;

	int success=st_hll_case_print(st_case, i);
	EXPECT_EQ(success+1, i);
}
TEST(STHyperLogLog, Debug)
{
	struct st_hll_case st_case[64];
	int i=0;
	st_hll_case_init(st_case+i);
	st_case[i].precision=14;
	st_case[i].time_window_ms=300;
	st_case[i].ideal_count=40000;
	st_case[i].n_replica=1;
	st_case[i].est_count=st_hll_test(st_case+i);
	i++;
	int success=st_hll_case_print(st_case, i);
	EXPECT_EQ(success, i);
}
TEST(STHyperLogLog, Replicas)
{
	
	int i=0, n_case=8;
	struct st_hll_case st_case[n_case];
	for(i=0; i<n_case; i++)
	{
		st_hll_case_init(st_case+i);
		st_case[i].precision=9;
		st_case[i].time_window_ms=1000*(i+1);
		st_case[i].ideal_count=8000;//2*1000*(1<<i);
		st_case[i].n_replica=1+i;
		st_case[i].sync_interval_ms=2;
		st_case[i].est_count=st_hll_test(st_case+i);
	}
	int success=st_hll_case_print(st_case, n_case);
	EXPECT_EQ(success, n_case);
}
TEST(STHyperLogLog, Serialize)
{
	struct timeval start;
	gettimeofday(&start, NULL);
	struct ST_hyperloglog *h=ST_hyperloglog_new(9, 5*1000, start);
	size_t sz=ST_hyperloglog_serialized_size(h);
	char *blob=NULL;
	size_t blob_sz=0;
	ST_hyperloglog_serialize(h, &blob, &blob_sz);
	EXPECT_EQ(sz, blob_sz);
	struct ST_hyperloglog *h2=ST_hyperloglog_deserialize(blob, blob_sz);
	free(blob);
	sz=ST_hyperloglog_serialized_size(h2);
	EXPECT_EQ(sz, blob_sz);
	ST_hyperloglog_free(h);
	ST_hyperloglog_free(h2);
}
TEST(STHyperLogLog, Reconfigure)
{
	struct timeval start, step, now;
	gettimeofday(&start, NULL);
	start.tv_usec=0;
	int n_replica=2;
	struct ST_hyperloglog *h[n_replica];
	int time_window_s=10;
	unsigned char precision=6;
	for(int i=0; i<n_replica; i++)
	{
		h[i]=ST_hyperloglog_new(precision, time_window_s*1000, start);
	}
	srand(173);
	int key=1319823, j=0;
	int n_add=0;
	int per_step_count=100;
	step.tv_sec=0;
	step.tv_usec=1000;
	int item_per_second=per_step_count*1000*1000/step.tv_usec;
	now=start;
	while(now.tv_sec-start.tv_sec<time_window_s*5)
	{
		timeradd(&now, &step, &now);
		for(int i=0; i<per_step_count; i++)
		{
			j=random()%n_replica;
			key++;
			ST_hyperloglog_add(h[j], (const char *)&key, sizeof(key), now);
			n_add++;
		}
	}
	st_hll_sync(h, n_replica);
	double hll_count=0, error=0, slack=1.2;
	hll_count=ST_hyperloglog_count(h[0], now);
	error=ST_hyperloglog_error_for_precision(precision);
	EXPECT_NEAR(hll_count, item_per_second*time_window_s, error*item_per_second*time_window_s*slack);

	time_window_s=20;
	precision=13;
	ST_hyperloglog_configure(h[0], precision, time_window_s*1000, now);
	st_hll_sync(h, n_replica);
	start=now;
	n_add=0;
	while(now.tv_sec-start.tv_sec<time_window_s*5)
	{
		timeradd(&now, &step, &now);
		for(int i=0; i<per_step_count; i++)
		{
			j=random()%n_replica;
			key++;
			ST_hyperloglog_add(h[j], (const char *)&key, sizeof(key), now);
			n_add++;
		}
	}
	st_hll_sync(h, n_replica);
	hll_count=ST_hyperloglog_count(h[0], now);
	error=ST_hyperloglog_error_for_precision(precision);
	EXPECT_NEAR(hll_count, item_per_second*time_window_s, error*item_per_second*time_window_s*slack);

	for(int i=0; i<n_replica; i++)
	{
		ST_hyperloglog_free(h[i]);
	}
}
TEST(STHyperLogLog, EventualConsistency)
{
	struct timeval start, step, now;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	int n_replica=2;
	struct ST_hyperloglog *h[n_replica];
	int time_window_s=10;
	unsigned char precision=6;
	for(int i=0; i<n_replica; i++)
	{
		h[i]=ST_hyperloglog_new(precision, time_window_s*1000, start);
	}
	int key=1319823, j=0;
	int n_add=0;
	int per_step_count=100;
	step.tv_sec=0;
	step.tv_usec=1000;
	int item_per_second=per_step_count*1000*1000/step.tv_usec;
	while(now.tv_sec-start.tv_sec<time_window_s*5)
	{
		timeradd(&now, &step, &now);
		for(int i=0; i<per_step_count; i++)
		{
			j=1+random()%(n_replica-1);
			key++;
			ST_hyperloglog_add(h[j], (const char *)&key, sizeof(key), now);
			n_add++;
		}
	}
	st_hll_sync(h, n_replica);
	double hll_count=0, error=0;
	hll_count=ST_hyperloglog_count(h[0], now);
	error=ST_hyperloglog_error_for_precision(precision);
	EXPECT_NEAR(hll_count, item_per_second*time_window_s, error*item_per_second*time_window_s*1.5);

	for(int i=0; i<n_replica; i++)
	{
		ST_hyperloglog_free(h[i]);
	}
}
TEST(STHyperLogLog, Merge)
{
	struct timeval start, now;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	int n_replica=2;
	struct ST_hyperloglog *h[n_replica];
	int time_window_s=10;
	unsigned char precision=6;
	for(int i=0; i<n_replica; i++)
	{
		h[i]=ST_hyperloglog_new(precision, time_window_s*1000, start);
		start.tv_sec+=time_window_s+1;
	}
	st_hll_sync(h, n_replica);
	for(int i=0; i<n_replica; i++)
	{
		ST_hyperloglog_free(h[i]);
	}
}
TEST(STHyperLogLog, Step)
{
	struct timeval start, step, now;
	gettimeofday(&start, NULL);
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=10000;
	int time_window_s=32;
	unsigned char precision=8;
	struct ST_hyperloglog *h=ST_hyperloglog_new(precision, time_window_s*1000, start);
	int key=1;
	int per_step_count=1;
	int item_per_second=per_step_count*1000*1000/step.tv_usec;
	while(now.tv_sec-start.tv_sec<time_window_s*10)
	{
		timeradd(&now, &step, &now);
		for(int i=0; i<per_step_count; i++)
		{
			key++;
			ST_hyperloglog_add(h, (const char *)&key, sizeof(key), now);
		}
	}
	double hll_count=0;
	double error=ST_hyperloglog_error_for_precision(precision);
	hll_count=ST_hyperloglog_count(h, now);
	printf("time_window: %d, count: %d, error: %.2f \n", time_window_s, item_per_second*time_window_s, error);
	for(int i=0; i<time_window_s; i++)
	{
		ST_hyperloglog_step(h, now);
		hll_count=ST_hyperloglog_count(h, now);
		printf("t+%d, estimate: %.2f\n", i, hll_count);
		now.tv_sec++;
		//EXPECT_NEAR(hll_count, item_per_second*(time_window_s-i), error*item_per_second*(time_window_s-i));
	}
	ST_hyperloglog_free(h);
	return;
}
TEST(STHyperLogLog, Microscope)
{
	const char *key[2]={"192.168.41.12", "192.168.41.11"};
	struct timeval start, step, now;
	start.tv_sec=1000;
	start.tv_usec=0;
	memcpy(&now, &start, sizeof(now));
	step.tv_sec=0;
	step.tv_usec=10*1000;
	struct ST_hyperloglog *h=ST_hyperloglog_new(7, 1000, start);
	double estimated=0;
	int passed=0, counted=0;
	while(now.tv_sec-start.tv_sec<10)
	{
		timeradd(&now, &step, &now);
		for(int i=0; i<2; i++)
		{
			ST_hyperloglog_add(h, key[i], strlen(key[i]), now);
		}
		estimated=ST_hyperloglog_count(h, now);
		if(abs(estimated-2)<0.1)
		{
			passed++;
		}
		counted++;
	}
	EXPECT_EQ(passed, counted);
	ST_hyperloglog_free(h);
}
void ap_bloom_sync(struct AP_bloom *list[], size_t n)
{
	char *blob=NULL;
	size_t blob_sz=0;
	if(n<2) return;
	for(size_t i=0; i<n; i++)
	{
		AP_bloom_serialize(list[i], &blob, &blob_sz);
		for(size_t j=0; j<n; j++)
		{
			if(j==i) continue;
			AP_bloom_merge_blob(list[j], blob, blob_sz);
		}
		free(blob);
		blob=NULL;
	}	
	return;
}

struct ap_bloom_case
{
	int n_replica;
	long long time_window_ms;
	long long time_slice_num;
	long long ideal_capacity;
	double ideal_error_rate;
	double crowdness;
};
void ap_bloom_test(struct ap_bloom_case *mycase)
{
	struct timeval start, now, step;
	gettimeofday(&start, NULL);
    start.tv_sec=1708094293;
    start.tv_usec=319085;
	struct AP_bloom *bloom[mycase->n_replica];
	for(int i=0; i<mycase->n_replica; i++)
	{
		bloom[i]=AP_bloom_new(start, mycase->ideal_error_rate, mycase->ideal_capacity, mycase->time_window_ms, mycase->time_slice_num);
	}
	srand(171);
	double per_second_count=(double)mycase->ideal_capacity * 1000 * mycase->crowdness/mycase->time_window_ms;
	int per_step_count=0;
	per_second_count=MAX(per_second_count, 1);
	if(per_second_count>1000*1000)
	{
		step.tv_sec=0;
		step.tv_usec=1;
		per_step_count=per_second_count/1000/1000;
	}
	else
	{
		step.tv_sec=0;
		step.tv_usec=(suseconds_t)1000*1000/per_second_count;
		per_step_count=1;
	}
	
	now=start;
	int key=1319823, started_key=key, most_recent_forgot_key=key;
	int added=0;
	struct timeval last_sync, most_recent_forgot_time;
	last_sync=now;
	long long sync_interval_ms = 100;
	long long test_duration_us = mycase->time_window_ms*1000*10;
	while(timeval_delta_us(start, now) < test_duration_us)
	{
		for(int i=0; i<per_step_count; i++)
		{
			int j=random()%mycase->n_replica;	
			if(test_duration_us - timeval_delta_us(start, now) >= mycase->time_window_ms*1000)
			{
				most_recent_forgot_key=key;
				most_recent_forgot_time=now;
			}
			AP_bloom_add(bloom[j], now, (char *)&key, sizeof(key));
			added++;
			key++;
		}
		if(timeval_delta_ms(now, last_sync) > sync_interval_ms)
		{
			ap_bloom_sync(bloom, mycase->n_replica);
			last_sync=now;
		}
		timeradd(&now, &step, &now);
	}
	ap_bloom_sync(bloom, mycase->n_replica);
	struct AP_bloom_info info;
	AP_bloom_info(bloom[0], &info);
	
	long long true_positive=0;
	int ret=0, checked=0;
	for(int i=most_recent_forgot_key+1; i<key; i++)
	{
		int j=random()%mycase->n_replica;
		ret = AP_bloom_check(bloom[j], now, (char *)&i, sizeof(i));
		true_positive += ret;
		assert(ret == 1);
		checked++;
	}
	EXPECT_EQ(true_positive, checked);
	long long false_positive=0;
	checked=0;
	for(int i=started_key; i<=most_recent_forgot_key; i++)
	{
		int j=random()%mycase->n_replica;
		ret = AP_bloom_check(bloom[j], now, (char *)&i, sizeof(i));
		//assert(ret == 0);
		false_positive += ret;
		checked++;
	}
	long long key_in_slice_plus_one = (key - most_recent_forgot_key + 1)/mycase->time_slice_num;
    //long long slack = per_second_count*mycase->time_window_ms/1000/mycase->time_slice_num;
    false_positive = MAX(0, false_positive-key_in_slice_plus_one);
	double fp_rate=(double)false_positive/(per_second_count*mycase->time_window_ms);
	//EXPECT_NEAR(fp_rate, mycase->ideal_error_rate, mycase->ideal_error_rate/2);
	//EXPECT_NEAR(tp_rate, 1, 0);
	size_t mem_size=AP_bloom_mem_size(bloom[0]);
	printf("fp: %.4f, element_num: %lld, approx_num: %lld, max_expand: %lld, fill_ratio: %.2f, slice_num: %lld, mem_size: %zu\n",
		fp_rate, (long long)(per_second_count*mycase->time_window_ms/1000), info.approximate_item_num,
         info.max_expand_times, info.fill_ratio, info.total_slice_number, mem_size);
	//printf("checked: %d, added: %d\n", checked, added);
	for(int i=0; i<mycase->n_replica; i++)
	{
		AP_bloom_free(bloom[i]);
	}
}
TEST(APBloom, Basic)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	long long time_window_ms=300;
	long long capacity=1000;
	double error_rate=0.001;
	struct AP_bloom *bloom=AP_bloom_new(now, error_rate, capacity, time_window_ms, 3);
	int key=11;
	int ret=0;
	struct timeval first_insert;
	first_insert=now;
	AP_bloom_add(bloom, first_insert, (char *)&key, sizeof(key));
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);

	now.tv_usec+=time_window_ms/10;
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);

	struct AP_bloom_info info;
	AP_bloom_info(bloom, &info);
	EXPECT_EQ(info.capacity, capacity);
	EXPECT_EQ(info.error, error_rate);
	EXPECT_EQ(info.time_window_ms, time_window_ms);
	EXPECT_TRUE(timercmp(&info.oldest_item_time, &first_insert, <=));

	now.tv_sec+=time_window_ms/1000+1;
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 0);
	key++;
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 0);

	AP_bloom_add(bloom, now, (char *)&key, sizeof(key));
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);



	//boudary test with invalid time.
	now.tv_sec = 0;
	ret=AP_bloom_check(bloom, now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 0);


	AP_bloom_free(bloom);
}
TEST(APBloom, NoSliding)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	struct AP_bloom *bloom[2];
	for(int i=0; i<2; i++)
	{
		bloom[i]=AP_bloom_new(now, 0.001, 1000, 0, 0);
	}
	int key=11;
	int n_item=1000;
	for(int i=0; i<n_item; i++)
	{
		AP_bloom_add(bloom[i%2], now, (char *)&key, sizeof(key));
		key++;
		now.tv_sec++;
	}
	ap_bloom_sync(bloom, 2);
	key=11;
	now.tv_sec += 100;
	int count=0;
	for(int i=0; i<n_item; i++)
	{
		count += AP_bloom_check(bloom[i%2], now, (char *)&key, sizeof(key));
		key++;
	}
	EXPECT_EQ(count, n_item);
	for(int i=0; i<2; i++)
	{
		AP_bloom_free(bloom[i]);
	}
}
TEST(APBloom, Merge)
{
	struct timeval start, now;
	gettimeofday(&start, NULL);
	now=start;
	long long timespan=300;
	struct AP_bloom *bloom[2];

	bloom[0]=AP_bloom_new(start, 0.001, 1000, timespan, 3);
	start.tv_sec--;
	bloom[1]=AP_bloom_new(start, 0.001, 1000, timespan, 3);
	int key=11;
	AP_bloom_add(bloom[0], now, (char *)&key, sizeof(key));
	int ret=AP_bloom_check(bloom[0], now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);
	ap_bloom_sync(bloom, 2);
	ret=AP_bloom_check(bloom[1], now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);

	for(int i=0; i<2; i++)
	{
		AP_bloom_free(bloom[i]);
	}

	//Test merge with different capacity and error rate.
	bloom[0]=AP_bloom_new(start, 0.001, 1000, timespan, 3);
	start.tv_sec++;
	long long new_capacity=10000;
	double new_error_rate=0.0001;
	bloom[1]=AP_bloom_new(start, new_error_rate, new_capacity, timespan, 3);
	AP_bloom_add(bloom[0], now, (char *)&key, sizeof(key));
	ret=AP_bloom_check(bloom[0], now, (char *)&key, sizeof(key));
	EXPECT_EQ(ret, 1);
	ap_bloom_sync(bloom, 2);
	ret=AP_bloom_check(bloom[1], now, (char *)&key, sizeof(key));
	//bloom[1] is newer than bloom[0], so results in bloom[0] is discarded.
	EXPECT_EQ(ret, 0);
	struct AP_bloom_info info;
	AP_bloom_info(bloom[0], &info);
	EXPECT_EQ(info.capacity, new_capacity);
	EXPECT_EQ(info.error, new_error_rate);
	for(int i=0; i<2; i++)
	{
		AP_bloom_free(bloom[i]);
	}
}
TEST(APBloom, Scalability)
{
	struct ap_bloom_case mycase[6]={
		{1, 300, 8, 1000, 0.001, 0.1},
		{1, 300, 8, 1000, 0.001, 1},
		{2, 300, 8, 1000, 0.001, 10},
		{2, 300, 8, 1000, 0.001, 20},
		{2, 300, 8, 1000, 0.001, 100},
		{2, 300, 8, 1000, 0.001, 200},
	};
	for(int i=0; i<6; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, VariousCapacity)
{
	struct ap_bloom_case mycase[3]={
		{2, 300, 13, 1000, 0.001, 1},
		{1, 300, 13, 10000, 0.001, 1},
		{2, 300, 13, 100000, 0.001, 2}
	};
	for(int i=0; i<3; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, VariousWindow)
{
	int n_case=8;
	struct ap_bloom_case mycase[n_case];
	
	for(int i=0; i<n_case; i++)
	{
		mycase[i].n_replica=2;
		mycase[i].time_window_ms=30*(1<<i);
		mycase[i].time_slice_num=32;
		mycase[i].ideal_capacity=10000;
		mycase[i].ideal_error_rate=0.001;
		mycase[i].crowdness=1.3;
	}
	for(int i=0; i<n_case; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, VariousReplica)
{
	int n_case=6;
	struct ap_bloom_case mycase[n_case];
	for(int i=0; i<n_case; i++)
	{
		mycase[i].n_replica=i+1;
		mycase[i].time_window_ms=30*1000;
		mycase[i].time_slice_num=16;
		mycase[i].ideal_capacity=10000;
		mycase[i].ideal_error_rate=0.001;
		mycase[i].crowdness=1.3;
	}
	for(int i=0; i<n_case; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, VariousTimeSlice)
{
	int n_case=7;
	struct ap_bloom_case mycase[n_case];
	for(int i=0; i<n_case; i++)
	{
		mycase[i].n_replica=2;
		mycase[i].time_window_ms=300*1000;
		mycase[i].time_slice_num=1<<i;
		mycase[i].ideal_capacity=10000;
		mycase[i].ideal_error_rate=0.001;
		mycase[i].crowdness=1.3;
	}
	for(int i=0; i<n_case; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, VariousError)
{
	int n_case=6;
	struct ap_bloom_case mycase[n_case];
	for(int i=0; i<n_case; i++)
	{
		mycase[i].n_replica=2;
		mycase[i].time_window_ms=300*1000;
		mycase[i].time_slice_num=16;
		mycase[i].ideal_capacity=10000;
		mycase[i].ideal_error_rate=0.10;
		for(int j=0; j<i; j++)
		{
			mycase[i].ideal_error_rate/=10;
		}
		mycase[i].crowdness=1.3;
	}
	for(int i=0; i<n_case; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, Performance)
{
	struct ap_bloom_case mycase[1]={
		{1, 300, 4, 100000, 0.001, 10}
	};
	for(int i=0; i<1; i++)
	{
		ap_bloom_test(mycase+i);
	}
}
TEST(APBloom, Debug)
{

}
int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
