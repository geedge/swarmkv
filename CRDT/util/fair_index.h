#include <stddef.h>

struct test_records
{
    int cnt;
    int succ_cnt;
    int fail_cnt;
};

struct fstb_class
{
	long long class_id;
	long long weight;
	long long requested_CIR;
	long long demand_tokens;
	long long allocated_tokens;
	long long ideal_tokens;
};
float calculate_jain_fair_index(struct test_records *result, struct test_records *class_result, unsigned char *class_weight, int class_num);
float max_min_fairness_index(long long available_tokens, struct fstb_class * classes, size_t n_class);