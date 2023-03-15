#include "ftb_throttler.h"
#include "crdt_utils.h"
#include "ransampl.h"
#include <cstddef>
#include <cstdio>
#include <gtest/gtest.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <math.h>
#include "util/flow_tokens.h"
#include "util/fair_index.h"

void ftbt_throttle_sync(struct ftb_throttler *list[], size_t n)
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
	long long CIR=100;
	long long CBS=200;
	struct timeval now;
	gettimeofday(&now, NULL);

	bucket=ftb_throttler_new(uuid, now, CIR, CBS, CIR/10);
	long long rtn = 0;

	rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 60, "1", 1);
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_FLEXIBLE, 200, "1", 1);
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 60, "1", 1);
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

	rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 200, "1", 1); //active list : 200
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 160, "2", 2);//active list : 200 160
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    timeradd(&now, &step, &now);
    rtn=ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_NORMAL, 20, "1", 1);//active list : 130 30
    ftb_throttler_info(bucket, info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 100);

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

	rtn=ftb_throttler_control(bucket[0], now, FTBT_CMD_CONSUME_NORMAL, 200, "1", 1);
    ftb_throttler_info(bucket[0], info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    rtn=ftb_throttler_control(bucket[1], now, FTBT_CMD_CONSUME_NORMAL, 50, "2", 1);
	ftb_throttler_info(bucket[1], info);
	EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    timeradd(&now, &step, &now);
    rtn=ftb_throttler_control(bucket[0], now, FTBT_CMD_CONSUME_NORMAL, 0, "1", 1); //300-200=100
	ftb_throttler_info(bucket[0], info);
    EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 100);
    rtn=ftb_throttler_control(bucket[1], now, FTBT_CMD_CONSUME_NORMAL, 0, "2", 1);//300-50=250(200)
	ftb_throttler_info(bucket[1], info);
    EXPECT_EQ(rtn, 1);
    EXPECT_EQ(info->available, 200);

    ftbt_throttle_sync(bucket, 2);

    ftb_throttler_info(bucket[0], info);
    EXPECT_EQ(info->available, 50);
	ftb_throttler_info(bucket[1], info);
    EXPECT_EQ(info->available, 50);
	
	ftb_throttler_free(bucket[0]);
    ftb_throttler_free(bucket[1]);
}

#define FAIR_INDEX_CIR 1*1024*1000 //1024 bit/ms
#define FAIR_INDEX_CBS 10*1024
#define FAIR_INDEX_REQ FAIR_INDEX_CIR/1000
#define FAIR_INDEX_MIMIC_DURATION_US 1*1000*1000

TEST(FTBT, Pareto)
{
    int class_num = 24;
    enum flow_type ftype = PARETO;
    uuid_t uuid;
    uuid_generate(uuid);
    struct timeval start;
    gettimeofday(&start, NULL);
    struct ftb_throttler_info *info = ALLOC(struct ftb_throttler_info, 1);
    struct ftb_throttler *bucket=NULL;
    bucket=ftb_throttler_new(uuid, start, FAIR_INDEX_CIR, FAIR_INDEX_CBS, FAIR_INDEX_CIR/1000/10);
    size_t i,j,k;
    long long tokens=0;
    long long consumed_class[class_num];
    long long consumed=0, requested=0, upper_limit=0, refilled=0;
    struct timeval step, now;
    memcpy(&now, &start, sizeof(now));
    step.tv_sec=0;
    step.tv_usec=(suseconds_t)1000;
    memset(consumed_class, 0, sizeof(consumed_class));
    char str[20];

    struct test_records ftbf_class_results[class_num], ftbf_result;
    unsigned char class_weight[class_num];
    double prob[class_num];
    memset(&ftbf_result, 0, sizeof(ftbf_result));
    memset(ftbf_class_results, 0, sizeof(ftbf_class_results));
    memset(class_weight, 0, sizeof(class_weight));
    memset(prob, 0, sizeof(prob));

    for(int i = 0; i < class_num; i++)
    {
        class_weight[i] = 1;
    }
    set_flow_prob(prob, class_num, ftype);
    ransampl_ws *ws = ransampl_init(prob, class_num);
    int round = FAIR_INDEX_MIMIC_DURATION_US/1000;
    srandom(17);
    for(i=0; i<(size_t)round; i++)
    {
        timeradd(&now, &step, &now);
        for(k = 0; k < (size_t)class_num; ++k)
        {
            j = ransampl_draw(ws); //并发但未限定每个classid最多请求一次，待修改
            tokens = 0.2*FAIR_INDEX_REQ+rand()%5;
            requested += tokens;
            sprintf(str, "%zu", j);
            int rtn = ftb_throttler_control(bucket, now, FTBT_CMD_CONSUME_FLEXIBLE, tokens, str, class_weight[j]);
            ftbf_result.cnt++;
            ftbf_class_results[j].cnt++;
            if(rtn > 0)
            {
                ftbf_result.succ_cnt++;
                ftbf_class_results[j].succ_cnt++;
            }else
            {
                ftbf_result.fail_cnt++;
                ftbf_class_results[j].fail_cnt++;
            }
        }
    }

    upper_limit=FAIR_INDEX_CBS+FAIR_INDEX_CIR/1000*timeval_delta_ms(start, now);
    ftb_throttler_info(bucket, info);
    refilled=info->refilled;
    for(i=0; i<(size_t)class_num; ++i)
    {
        consumed += consumed_class[i];
        // printf("rate[%zu]=%.2lld\n", i, consumed_class[i]);
    }
    EXPECT_LE(consumed, requested);
    double accuracy=(double)consumed/MIN(refilled, requested);

    float jain_fair_index = calculate_jain_fair_index(&ftbf_result, ftbf_class_results, class_weight, class_num);
    printf("accuracy:%f, upper_limit:%lld, refilled:%lld, requested:%lld, consumed:%lld, jain_fair_index:%f\n",
           accuracy,
           upper_limit,
           refilled,
           requested,
           consumed,
           jain_fair_index);
    ransampl_free(ws);
    ftb_throttler_free(bucket);
    EXPECT_NEAR(jain_fair_index, 1, 0.01);
}


int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
	ret=RUN_ALL_TESTS();
	return ret;
}
