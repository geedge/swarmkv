#include "ftb_throttler.h"
#include "crdt_utils.h"
#include "ransampl.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <math.h>
#include "util/flow_tokens.h"
#include "util/fair_index.h"

#define FAIR_INDEX_CIR 1*1000*1000 //1000 bit/ms, 1M/s
#define FAIR_INDEX_CBS 10*1000
#define FAIR_INDEX_REQ FAIR_INDEX_CIR/1000
#define FAIR_INDEX_MIMIC_DURATION_US 100*1000*1000

void ftb_throttler_sync(struct ftb_throttler *list[], size_t n)
{
    char *blob=NULL;
    size_t blob_sz=0;
    for(size_t i=0; i<n; i++)
    {
        ftb_throttler_serialize(list[i], &blob, &blob_sz);
        for(size_t j=0; j<n; j++)
        {
            if(j==i) continue;
            ftb_throttler_merge_blob(list[j], blob, blob_sz);
        }
        free(blob);
        blob=NULL;
    }
}

TEST(FTBT, Basic)
{
    uuid_t uuid;
	uuid_generate(uuid);

	struct ftb_throttler *bucket=NULL;
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
	long long CIR=100*1000;
	long long CBS=200;
	struct timeval now;
	gettimeofday(&now, NULL);

	bucket=ftb_throttler_new(uuid, now, CIR, CBS, CIR/10);
	long long rtn = 0;

	rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 60, 1, 1);
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_FLEXIBLE, 200, 1, 1);
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 60, 1, 1);
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 0);
    EXPECT_EQ(info->available, 200);
	
	ftb_throttler_free(bucket);
}

TEST(FTBT, BasicWeighted)
{
    uuid_t uuid;
	uuid_generate(uuid);

	struct ftb_throttler *bucket=NULL;
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
	long long CIR=100*1000; //100*1000 bps
	long long CBS=200;
	struct timeval now, step;
	gettimeofday(&now, NULL);

    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;

	bucket=ftb_throttler_new(uuid, now, CIR, CBS, CIR/10);
	long long rtn = 0;

	rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 200, 1, 1); //active list : 200
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 160, 2, 2);//active list : 200 160
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    timeradd(&now, &step, &now);
    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 20, 1, 1);//active list : 130 40
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 0);
    EXPECT_EQ(info->available, 110);

	ftb_throttler_free(bucket);
}

TEST(FTBT, Merge)
{
    uuid_t uuid;

	struct ftb_throttler *bucket[2];
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
	long long CIR=100*1000; //100*1000 bps
	long long CBS=200;
    long long rtn = 0;
	struct timeval now, step;
	gettimeofday(&now, NULL);

    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;

    uuid_generate(uuid);
	bucket[0]=ftb_throttler_new(uuid, now, CIR, CBS, CIR/10);
    uuid_generate(uuid);
    bucket[1]=ftb_throttler_new(uuid, now, CIR, CBS, CIR/10);

	rtn=ftb_throttler_control(bucket[0], now, FTBT_CMD_CONSUME_NORMAL, 200, 1, 1);
    ftb_throttler_info(bucket[0], info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket[1], now, FTBT_CMD_CONSUME_NORMAL, 50, 2, 1);
	ftb_throttler_info(bucket[1], info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    timeradd(&now, &step, &now);
    rtn=ftb_throttler_control(bucket[0], now, FTBT_CMD_CONSUME_NORMAL, 0, 1, 1); //300-200=100
	ftb_throttler_info(bucket[0], info);
    EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 100);
    rtn=ftb_throttler_control(bucket[1], now, FTBT_CMD_CONSUME_NORMAL, 0, 2, 1);//300-50=250(200)
	ftb_throttler_info(bucket[1], info);
    EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    ftb_throttler_sync(bucket, 2);

    ftb_throttler_info(bucket[0], info);
    EXPECT_EQ(info->available, 50);
	ftb_throttler_info(bucket[1], info);
    EXPECT_EQ(info->available, 50);
	
	ftb_throttler_free(bucket[0]);
    ftb_throttler_free(bucket[1]);
}

TEST(FTBT, SingleReplica)
{
    int class_num = 24;
    enum flow_type ftype = PARETO;
    uuid_t uuid;
    uuid_generate(uuid);
    struct timeval start;
    gettimeofday(&start, NULL);
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
    struct ftb_throttler *bucket=NULL;
    bucket=ftb_throttler_new(uuid, start, FAIR_INDEX_CIR, FAIR_INDEX_CBS, FAIR_INDEX_CIR/10);
    size_t i,j,k;
    long long tokens=0;
    long long consumed=0, requested=0, upper_limit=0, refilled=0;
    struct timeval step, now;
    memcpy(&now, &start, sizeof(now));
    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;

    double prob[class_num];
    struct fstb_class pareto_classes[class_num];
    memset(prob, 0, sizeof(prob));
    memset(pareto_classes, 0, sizeof(pareto_classes));
    for(i = 0; i < (size_t)class_num; i++)
    {
        pareto_classes[i].class_id = i+1;
        pareto_classes[i].weight = 1;
    }
    long double sum_w = set_flow_prob(prob, class_num, ftype);
    ransampl_ws *ws = ransampl_init(prob, class_num);
    int round = FAIR_INDEX_MIMIC_DURATION_US/1000;
    srandom(17);
    for(i=0; i<(size_t)round; i++)
    {
        timeradd(&now, &step, &now);
        for(k = 0; k < (size_t)class_num; ++k)
        {
            j = ransampl_draw(ws); 
            tokens = (long long)(sum_w*FAIR_INDEX_REQ/class_num);
            requested += tokens;
            pareto_classes[j].demand_tokens += tokens;
            int rtn = ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, tokens, j, pareto_classes[j].weight);
            if(rtn > 0)
            {   
                pareto_classes[j].allocated_tokens += tokens;
            }
        }
    }

    upper_limit=FAIR_INDEX_CBS+FAIR_INDEX_CIR/1000*timeval_delta_ms(start, now);
    ftb_throttler_info(bucket, info);
    refilled=info->refilled;
    consumed=info->consumed;
    EXPECT_LE(consumed, requested);
    double accuracy=(double)consumed/MIN(refilled, requested);

    float fair_index = max_min_fairness_index(refilled, pareto_classes, class_num);
    int print=1;
	if(print)
	{
		printf("class\tweight\tdemand\tallocated\tideal\r\n");
        long duration_s = FAIR_INDEX_MIMIC_DURATION_US/1000/1000;
		for(size_t i=0; i<(size_t)class_num; i++)
		{
			printf("%lld\t%lld\t%lld\t%lld\t%lld\r\n", pareto_classes[i].class_id,
										pareto_classes[i].weight,
										pareto_classes[i].demand_tokens/duration_s,
										pareto_classes[i].allocated_tokens/duration_s,
										pareto_classes[i].ideal_tokens/duration_s);
		}
		printf("CIR %d fairness index %f\r\n", FAIR_INDEX_CIR, fair_index);
	}
    printf("accuracy:%f, upper_limit:%lld, refilled:%lld, requested:%lld, consumed:%lld\n",
           accuracy,
           upper_limit,
           refilled,
           requested,
           consumed);
    ransampl_free(ws);
    ftb_throttler_free(bucket);
    EXPECT_NEAR(fair_index, 1, 0.1);
}

TEST(FTBT, SingleReplicaOneHeavy)
{
    int class_num = 24;
    enum flow_type ftype = ONEHEAVY;
    uuid_t uuid;
    uuid_generate(uuid);
    struct timeval start;
    gettimeofday(&start, NULL);
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
    struct ftb_throttler *bucket=NULL;
    bucket=ftb_throttler_new(uuid, start, FAIR_INDEX_CIR, FAIR_INDEX_CBS, FAIR_INDEX_CIR/10);
    size_t i,j,k;
    long long tokens=0;
    long long consumed=0, requested=0, upper_limit=0, refilled=0;
    struct timeval step, now;
    memcpy(&now, &start, sizeof(now));
    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;

    double prob[class_num];
    struct fstb_class one_heavy_classes[class_num];
    memset(prob, 0, sizeof(prob));
    memset(one_heavy_classes, 0, sizeof(one_heavy_classes));
    for(i = 0; i < (size_t)class_num; i++)
    {
        one_heavy_classes[i].class_id = i+1;
        one_heavy_classes[i].weight = 1;
    }
    long double sum_w = set_flow_prob(prob, class_num, ftype);
    ransampl_ws *ws = ransampl_init(prob, class_num);
    int round = FAIR_INDEX_MIMIC_DURATION_US/1000;
    srandom(17);
    for(i=0; i<(size_t)round; i++)
    {
        timeradd(&now, &step, &now);
        for(k = 0; k < (size_t)class_num; ++k)
        {
            j = ransampl_draw(ws); 
            tokens = (long long)(sum_w*FAIR_INDEX_REQ/class_num);
            requested += tokens;
            one_heavy_classes[j].demand_tokens += tokens;
            int rtn = ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, tokens, j, one_heavy_classes[j].weight);
            if(rtn > 0)
            {
               one_heavy_classes[j].allocated_tokens += tokens;
            }
        }
    }

    upper_limit=FAIR_INDEX_CBS+FAIR_INDEX_CIR/1000*timeval_delta_ms(start, now);
    ftb_throttler_info(bucket, info);
    refilled=info->refilled;
    consumed=info->consumed;
    EXPECT_LE(consumed, requested);
    double accuracy=(double)consumed/MIN(refilled, requested);

    float fair_index = max_min_fairness_index(refilled, one_heavy_classes, class_num);
    int print=1;
	if(print)
	{
		printf("class\tweight\tdemand\tallocated\tideal\r\n");
        long duration_s = FAIR_INDEX_MIMIC_DURATION_US/1000/1000;
		for(size_t i=0; i<(size_t)class_num; i++)
		{
			printf("%lld\t%lld\t%lld\t%lld\t%lld\r\n", one_heavy_classes[i].class_id,
										one_heavy_classes[i].weight,
										one_heavy_classes[i].demand_tokens/duration_s,
										one_heavy_classes[i].allocated_tokens/duration_s,
										one_heavy_classes[i].ideal_tokens/duration_s);
		}
		printf("CIR %d fairness index %f\r\n", FAIR_INDEX_CIR, fair_index);
	}
    printf("accuracy:%f, upper_limit:%lld, refilled:%lld, requested:%lld, consumed:%lld\n",
           accuracy,
           upper_limit,
           refilled,
           requested,
           consumed);
    ransampl_free(ws);
    ftb_throttler_free(bucket);
    EXPECT_NEAR(fair_index, 1, 0.1);
}

TEST(FTBT, SingleReplicaShake)
{
    int class_num = 5;
    enum flow_type ftype = UNIFORM;
    uuid_t uuid;
    uuid_generate(uuid);
    struct timeval start;
    gettimeofday(&start, NULL);
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
    struct ftb_throttler *bucket=NULL;
    bucket=ftb_throttler_new(uuid, start, FAIR_INDEX_CIR, FAIR_INDEX_CBS, FAIR_INDEX_CIR/10);
    size_t i,j,k;
    long long tokens=0;
    long long consumed=0, requested=0, upper_limit=0, refilled=0;
    struct timeval step, now;
    memcpy(&now, &start, sizeof(now));
    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;

    double prob[class_num];
    struct fstb_class shake_classes[class_num];
    memset(prob, 0, sizeof(prob));
    memset(shake_classes, 0, sizeof(shake_classes));
    for(i = 0; i < (size_t)class_num; i++)
    {
        shake_classes[i].class_id = i+1;
        shake_classes[i].weight = 1;
    }
    long double sum_w = set_flow_prob(prob, class_num, ftype);
    ransampl_ws *ws = ransampl_init(prob, class_num);
    int round = FAIR_INDEX_MIMIC_DURATION_US/1000;
    srandom(17);
    for(i=0; i<(size_t)round; i++)
    {
        timeradd(&now, &step, &now);
        for(k = 0; k < (size_t)class_num; ++k)
        {
            j = ransampl_draw(ws);
            if((size_t)(random()%class_num) > j)
                continue;
            tokens = (long long)(sum_w*FAIR_INDEX_REQ/class_num);
            requested += tokens;
            shake_classes[j].demand_tokens += tokens;
            int rtn = ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, tokens, j, shake_classes[j].weight);
            if(rtn > 0)
            {
                shake_classes[j].allocated_tokens += tokens;
            }
        }
    }

    upper_limit=FAIR_INDEX_CBS+FAIR_INDEX_CIR/1000*timeval_delta_ms(start, now);
    ftb_throttler_info(bucket, info);
    refilled=info->refilled;
    consumed=info->consumed;
    EXPECT_LE(consumed, requested);
    double accuracy=(double)consumed/MIN(refilled, requested);

    float fair_index = max_min_fairness_index(refilled, shake_classes, class_num);
    int print=1;
	if(print)
	{
		printf("class\tweight\tdemand\tallocated\tideal\r\n");
        long duration_s = FAIR_INDEX_MIMIC_DURATION_US/1000/1000;
		for(size_t i=0; i<(size_t)class_num; i++)
		{
			printf("%lld\t%lld\t%lld\t%lld\t%lld\r\n", shake_classes[i].class_id,
										shake_classes[i].weight,
										shake_classes[i].demand_tokens/duration_s,
										shake_classes[i].allocated_tokens/duration_s,
										shake_classes[i].ideal_tokens/duration_s);
		}
		printf("CIR %d fairness index %f\r\n", FAIR_INDEX_CIR, fair_index);
	}
    printf("accuracy:%f, upper_limit:%lld, refilled:%lld, requested:%lld, consumed:%lld\n",
           accuracy,
           upper_limit,
           refilled,
           requested,
           consumed);
    ransampl_free(ws);
    ftb_throttler_free(bucket);
    EXPECT_NEAR(fair_index, 1, 0.1);
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
    // ::testing::GTEST_FLAG(filter) = "FTBT.SingleReplicaShake";
	ret=RUN_ALL_TESTS();
	return ret;
}
