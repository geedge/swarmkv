#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <gtest/gtest.h>
#include <getopt.h>
#include "ap_bloom.h"
#include "crdt_utils.h"
#include "fieldstat/fieldstat_easy.h"

static unsigned int BM_CAPACITY = 10000000;
static double BM_ERROR_RATE = 0.000001d;
static int BM_TIMEOUT = 10 * 1000; // ms
static int BM_SLICE_NUM = 3;
static int MAX_ITEM_NUM = 10000000;
static int ITEM_BATCH_NUM = 1000;
#define TUPLE4_ADDR_LEN (12 + 1) // sizeof(tuple4) add begin char 'Y' or 'N'
#define BM_TEST_MAX_THREAD (1)
static pthread_t bm_test_thread_id[BM_TEST_MAX_THREAD];
static struct timeval g_current_time_tv;
static const char *g_user_define_args;

static const unsigned int INIT_SIP = 0x12345678;
static const unsigned int INIT_DIP = 0x87654321;
static const unsigned short INIT_SPORT = 0x1234;
static const unsigned short INIT_DPORT = 0x4321;

static long g_time_increment_ms = 0;
static struct timeval g_time_increment_tv = {0, 0};
static int g_force_timeout = 0;
static struct fieldstat_easy *fs4_instance;
static struct fieldstat_tag FS4_HISGRAM_TAG;
static int fs4_add_metric_id, fs4_check_true_metric_id, fs4_check_false_metric_id;

static inline void bm_update_key(char *tuple4_buf, unsigned int index)
{
    unsigned int *p_sip = (unsigned int *)&tuple4_buf[1];
    unsigned int *p_dip = (unsigned int *)&tuple4_buf[5];
    unsigned short *p_sport = (unsigned short *)&tuple4_buf[9];
    unsigned short *p_dport = (unsigned short *)&tuple4_buf[11];

    *p_sip = INIT_SIP + index;
    *p_dip = INIT_DIP + index;
    *p_sport = INIT_SPORT + index;
    *p_dport = INIT_DPORT + index;
}

static unsigned long long max_add_time = 0;      // ns
static unsigned long long total_add_time = 0;    // ns
static unsigned long long max_search_time = 0;   // ns
static unsigned long long total_search_time = 0; // ns

// for test, only support one thread yet
static struct timespec start_time, end_time;

static inline unsigned long long bm_time_diff(const struct timespec *start_time, const struct timespec *end_time)
{
    if (start_time->tv_sec == end_time->tv_sec)
    {
        return (unsigned long long)(end_time->tv_nsec - start_time->tv_nsec);
    }
    return ((unsigned long long)end_time->tv_sec * 1000 *1000*1000 + end_time->tv_nsec) - ((unsigned long long)start_time->tv_sec * 1000 *1000*1000 + start_time->tv_nsec);
}

static void bm_add_item(struct AP_bloom *bloom_filter, char *tuple4_buf, unsigned int index)
{
    struct timeval current_time_tv;
    bm_update_key(tuple4_buf, index);
    clock_gettime(CLOCK_REALTIME, &start_time);
    current_time_tv.tv_sec = start_time.tv_sec;
    current_time_tv.tv_usec = start_time.tv_nsec / 1000;
    if(g_force_timeout){
        timeradd(&current_time_tv, &g_time_increment_tv, &current_time_tv);
    }
    AP_bloom_add(bloom_filter,current_time_tv, tuple4_buf, TUPLE4_ADDR_LEN);
    clock_gettime(CLOCK_REALTIME, &end_time);

    unsigned long long time_diff = bm_time_diff(&start_time, &end_time);
    if (time_diff > max_add_time)
    {
        max_add_time = time_diff;
    }
    total_add_time += (unsigned long long)time_diff;

    if(fs4_instance){
        fieldstat_easy_histogram_record(fs4_instance, 0, fs4_add_metric_id, &FS4_HISGRAM_TAG, 1, time_diff);
    }
}

static int bm_search_item(struct AP_bloom *bloom_filter, char *tuple4_buf, unsigned int index, int fs4_metric_id)
{
    struct timeval current_time_tv;
    bm_update_key(tuple4_buf, index);
    clock_gettime(CLOCK_REALTIME, &start_time);
    current_time_tv.tv_sec = start_time.tv_sec;
    current_time_tv.tv_usec = start_time.tv_nsec / 1000;
    if(g_force_timeout){
        timeradd(&current_time_tv, &g_time_increment_tv, &current_time_tv);
    }    
    int ret = AP_bloom_check(bloom_filter, current_time_tv, tuple4_buf, TUPLE4_ADDR_LEN);
    clock_gettime(CLOCK_REALTIME, &end_time);
    unsigned long long time_diff = bm_time_diff(&start_time, &end_time);
    if (time_diff > max_search_time)
    {
        max_search_time = time_diff;
    }
    total_search_time += (unsigned long long)time_diff;

    if(fs4_instance){
        fieldstat_easy_histogram_record(fs4_instance, 0, fs4_metric_id, &FS4_HISGRAM_TAG, 1, time_diff);
    }
    return ret;
}

static int apbm_test(void)
{
    FS4_HISGRAM_TAG.key = "time";
    FS4_HISGRAM_TAG.type = TAG_DOUBLE;
    FS4_HISGRAM_TAG.value_double = 0.00001;

    fs4_instance = fieldstat_easy_new(1, "apbm-test", &FS4_HISGRAM_TAG, 1);
    fieldstat_easy_enable_auto_output(fs4_instance, "./apbm_gtest_fs4.log", 1);
    // fieldstat_easy_enable_delta_in_active_output(fs4_instance);
    fs4_add_metric_id = fieldstat_easy_register_histogram(fs4_instance, "add", 1, 99999999, 5);
    fs4_check_true_metric_id = fieldstat_easy_register_histogram(fs4_instance, "chk_true", 1, 99999999, 5);
    fs4_check_false_metric_id = fieldstat_easy_register_histogram(fs4_instance, "chk_false", 1, 99999999, 5);

    struct timeval current_time_tv;
    gettimeofday(&current_time_tv, NULL);
    
    struct AP_bloom *bm_handle = AP_bloom_new(g_current_time_tv, BM_ERROR_RATE, BM_CAPACITY, BM_TIMEOUT, BM_SLICE_NUM);
    assert(bm_handle);
    char tuple4_buf[1024] = {};
    long long search_y_error_num = 0;
    long long search_n_error_num = 0;

    tuple4_buf[0] = 'Y';

    if(g_force_timeout){
        g_time_increment_tv.tv_sec = BM_TIMEOUT / 1000 ;
        g_time_increment_tv.tv_usec = (BM_TIMEOUT % 1000) * 1000;
    }
    printf("starting test, capacity:%u, timeout:%dms, slice_num:%d, batch_num:%d, max-items:%d\n",
           BM_CAPACITY, BM_TIMEOUT, BM_SLICE_NUM, ITEM_BATCH_NUM, MAX_ITEM_NUM);
    int ret;
    int add_index = 0, search_index_y = 0, search_index_n = 0;
    while (add_index < MAX_ITEM_NUM || search_index_y < MAX_ITEM_NUM || search_index_n < MAX_ITEM_NUM)
    {
        tuple4_buf[0] = 'Y';
        for (int b = 0; b < ITEM_BATCH_NUM && add_index < MAX_ITEM_NUM; b++, add_index++)
        {
            bm_add_item(bm_handle, tuple4_buf, add_index);
        }

        tuple4_buf[0] = 'Y';
        for (int b = 0; b < ITEM_BATCH_NUM && search_index_y < MAX_ITEM_NUM; b++, search_index_y++)
        {
            ret = bm_search_item(bm_handle, tuple4_buf, search_index_y, fs4_check_true_metric_id);
            if (ret <= 0) // expect exist
            {
                search_y_error_num++;
            }
        }

        tuple4_buf[0] = 'N';
        for (int b = 0; b < ITEM_BATCH_NUM && search_index_n < MAX_ITEM_NUM; b++, search_index_n++)
        {
            ret = bm_search_item(bm_handle, tuple4_buf, search_index_n, fs4_check_false_metric_id);
            if (ret > 0) // expect not exist
            {
                search_n_error_num++;
            }
        }    
	}
    printf("---------------------------------------------------------------\n");
    printf("add %lld items success, avg-time:%.3fns, max:%lluns\n",
           (long long)MAX_ITEM_NUM, (double)total_add_time / (double)MAX_ITEM_NUM, max_add_time);

    printf("search %lld items , avg-time:%.3fns, max:%lluns\n",
           (long long)MAX_ITEM_NUM, (double)total_search_time / (double)MAX_ITEM_NUM, max_search_time);

    double err_rate = (double)search_n_error_num / (double)MAX_ITEM_NUM;
    printf("search_y_error_num:%lld \nsearch_n_error_num:%lld, errer-rate:%f\n",
           search_y_error_num, search_n_error_num, err_rate);
    printf("---------------------------------------------------------------\n");
    AP_bloom_free(bm_handle);

    sleep(2); //wait fs4 output
    fieldstat_easy_free(fs4_instance);
    return search_y_error_num + (err_rate > (double)BM_ERROR_RATE);
}

static void *bm_test_thread(void *arg)
{
    if (apbm_test() != 0)
    {
        return (void *)"error";
    }
    return (void *)"success";
}



static void bm_test_usage(void)
{
    printf("BM test usage:\n");
    printf("\tcapacity=2500000\n");
    printf("\terror_rate=0.000001\n");
    printf("\ttimeout=10000\n");
    printf("\tmax_item_num=10000000\n");
    printf("\titem_batch_num=1000\n");
    printf("\tforce_timeout=0\n");
    printf("\texample: ./ap_bloom_gtest -f=apbloom.perf -u=\"capacity=100000,error_rate=0.000001,timeout=10000,slice_num=3,max_item_num=100000,item_batch_num=1000,force_timeout=0\"\n");
    printf("\n");
}

static int convert_user_args(const char *user_args)
{
    if (user_args == NULL)
    {
        return 0;
    }
    if(strstr(user_args, "-h") ){
        bm_test_usage();
        exit(0);
    }
    if(strstr(user_args, "--help") ){
        bm_test_usage();
        exit(0);
    }
    
    char *tmp_args = strdup(user_args);
    char *saveptr = NULL;
    char *token = strtok_r(tmp_args, ",", &saveptr);
    while (token)
    {
        char *key = strtok(token, "=");
        char *value = strtok(NULL, "=");
        if (key && value)
        {
            if (strcmp(key, "capacity") == 0)
            {
                BM_CAPACITY = (unsigned int )strtoull(value, NULL, 10);
            }
            else if (strcmp(key, "error_rate") == 0)
            {
                BM_ERROR_RATE = strtod(value, NULL);
            }
            else if (strcmp(key, "timeout") == 0)
            {
                BM_TIMEOUT = atoi(value);
            }
            else if (strcmp(key, "max_item_num") == 0)
            {
                MAX_ITEM_NUM = (unsigned int )strtoull(value, NULL, 10);
            }
            else if (strcmp(key, "item_batch_num") == 0)
            {
                ITEM_BATCH_NUM = atoi(value);
            }
            else if (strcmp(key, "slice_num") == 0)
            {
                BM_SLICE_NUM = atoi(value);
            }
            else if (strcmp(key, "force_timeout") == 0)
            {
                g_force_timeout = atoi(value);
            }else{
                printf("unknown args: %s=%s\n", key, value);
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    free(tmp_args);

    return 0;
}
int ap_bloom_filter_test(const char *user_define_args)
{
    int ret = 0;
    convert_user_args(user_define_args);
    if (g_force_timeout)
    {
        g_current_time_tv.tv_sec = BM_TIMEOUT / 1000;
        g_current_time_tv.tv_usec = (BM_TIMEOUT % 1000) * 1000;
    }
    for (int i = 0; i < BM_TEST_MAX_THREAD; i++)
    {
        pthread_create(&bm_test_thread_id[i], NULL, bm_test_thread, NULL);
    }

    void *thread_result;
    for (int i = 0; i < BM_TEST_MAX_THREAD; i++)
    {
        pthread_join(bm_test_thread_id[i], &thread_result);
        if (strcmp((char *)thread_result, "success") != 0)
        {
            printf("thread %d test failed\n", i);
            ret = -1;
        }
    }
    return ret;
}

TEST(apbloom, perf)
{
    EXPECT_EQ(0, ap_bloom_filter_test(g_user_define_args));
}

static int apbloom_merge_test_run(void)
{
    int ret = 0;
    const int max_item_num = 1000000;
    struct timeval current_time_tv;
    gettimeofday(&current_time_tv, NULL);

    struct AP_bloom *apbma = (struct AP_bloom *)AP_bloom_new(g_current_time_tv, BM_ERROR_RATE, BM_CAPACITY, 1000 * 999, BM_SLICE_NUM);
    assert(apbma);
    char tuple4_buf[1024] = {};

    tuple4_buf[0] = 'A';
    for(int i = 0; i < max_item_num; i++){
        bm_add_item(apbma, tuple4_buf, i);
    }

    struct AP_bloom *apbmb = (struct AP_bloom *)AP_bloom_new(g_current_time_tv, BM_ERROR_RATE, BM_CAPACITY, 1000 * 999, BM_SLICE_NUM);
    assert(apbmb);
    
    tuple4_buf[0] = 'B';
    for(int i = 0; i < max_item_num; i++){
        bm_add_item(apbmb, tuple4_buf, i);
    }

    tuple4_buf[0] = 'B';
    for(int i = 0; i < max_item_num; i++){
        ret = bm_search_item(apbma, tuple4_buf, i, -1);
        if(ret > 0){
            fprintf(stderr, "found tuple4 with B apbma before merge!\n");
            return -1;
        }  
    }

    AP_bloom_merge(apbma, apbmb);

    for(int i = 0; i < max_item_num; i++){
        tuple4_buf[0] = 'A';
        ret = bm_search_item(apbma, tuple4_buf, i, -1);
        if(ret <= 0){
            fprintf(stderr, "not found tuple4 with A after merge, index:%d\n", i);
            return -1;
        }
        tuple4_buf[0] = 'B';
        ret = bm_search_item(apbma, tuple4_buf, i, -1);
        if(ret <= 0){
            fprintf(stderr, "not found tuple4 with B after merge, index:%d\n", i);
            return -1;
        }        
    }
    AP_bloom_free(apbma);
    AP_bloom_free(apbmb);
    return 0;
}

TEST(apbloom, merge)
{
    EXPECT_EQ(0, apbloom_merge_test_run());
}

static const char *gtest_cla_short_options = "hLf:u:";

static const struct option gtest_cla_long_options[] =
{
    {"help", no_argument, NULL, 'h'},
    {"gtest_list_tests", no_argument, NULL, 'L'},
    {"gtest_filter", required_argument, NULL, 'f'},
    {"user-define", required_argument, NULL, 'u'},
    {NULL, 0, NULL, 0}
};

static void usage(int argc, char *argv[])
{
	printf("args example:\n"); 
	printf("\t%s short-arg long-arg\n", argv[0]);
	printf("\t%s -v \t--version\n", argv[0]);
	printf("\t%s -L \t--gtest_list_tests\n", argv[0]);
	printf("\t%s -u \t--user-define\n", argv[0]);
	printf("\t%s -f \t--gtest_filter=tcp.simple\n", argv[0]);
	printf("\t%s -f \t--gtest_filter=tcp*\n", argv[0]);
	printf("\t%s -f \t--gtest_filter=apbloom.* -u \"capacity=2500000,error_rate=0.000001\"\n", argv[0]);
	exit(0);
}

int main(int argc, char *argv[])
{
    int c, ret = 0;
	int to_gtest_argc = 1;
	char *to_gtest_args[4] = {(char *)"ap_bloom_gtest", NULL,NULL,NULL};
    char temp_string[1024] = {};
	while(1){
		c = getopt_long(argc, argv, gtest_cla_short_options, gtest_cla_long_options, NULL);
		if(c == -1){
			break;	
		}

		switch(c){

		case 'h':
			usage(argc, argv);
		break;

		case 'L':
			to_gtest_args[1] = (char *)"--gtest_list_tests";
			to_gtest_argc++;
		break;

		case 'f':
			strncpy(temp_string, (char *)"--gtest_filter=", strlen("--gtest_filter="));
			strcat(temp_string, optarg);
			to_gtest_argc++;
			to_gtest_args[1] = temp_string;
		break;		

		case 'u':
			g_user_define_args = optarg;
		break;

		case '?': /* invalid or unknown option */
			return -1;
		break;
		}
	}

    ::testing::InitGoogleTest(&to_gtest_argc, to_gtest_args);
    ret = RUN_ALL_TESTS();
    return ret;
}
