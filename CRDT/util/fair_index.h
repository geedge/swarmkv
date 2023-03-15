#include <stddef.h>

struct test_records
{
    int cnt;
    int succ_cnt;
    int fail_cnt;
};

float calculate_jain_fair_index(struct test_records *result, struct test_records *class_result, unsigned char *class_weight, int class_num);