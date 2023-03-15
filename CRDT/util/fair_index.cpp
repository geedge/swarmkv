#include "fair_index.h"
#include <stdint.h>
#include "math.h"
#include "stdio.h"

float calculate_jain_fair_index(struct test_records *result, struct test_records *class_result, unsigned char *class_weight, int class_num)
{
    float succ_sum=0.0, succ_square_sum=0.0;

    uint32_t weight_sum = 0;
    for(int i = 0; i < class_num; i++)
    {
        weight_sum += *(class_weight+i);
    }
    float avg_weigth = (float)weight_sum/class_num;

    // Jain's fairness index is given by (sum(x_i))^2/n*sum(x_i^2)
    for(int i = 0; i < class_num; i++)
    {
        struct test_records *p_res = class_result+i;
        int weight = *(class_weight+i);
        float weight_factor = avg_weigth/(float)(*(class_weight+i));
        int expect_cnt = result->cnt/weight_sum*weight;
        printf("class_index:%d, class_weigth:%d, weight_factor:%f , expect_cnt=%d --- total_cnt:%d, succ_cnt:%d fail_cnt:%d\n", i, *(class_weight+i), weight_factor, expect_cnt, p_res->cnt, p_res->succ_cnt, p_res->fail_cnt);
        succ_sum += p_res->succ_cnt; // sum of x_i
        succ_square_sum += pow(p_res->succ_cnt,2);// sum of x_i^2
    }
    printf("total:%d, succ:%d, fail:%d\n", result->cnt, result->succ_cnt, result->fail_cnt);
    return (float)(pow(succ_sum,2))/(float)(class_num*succ_square_sum);
}
