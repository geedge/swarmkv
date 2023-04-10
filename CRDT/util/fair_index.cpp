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

int cmp_fstb_class(const void *a, const void *b)
{
	struct fstb_class *ra=(struct fstb_class*)a;
	struct fstb_class *rb=(struct fstb_class*)b;
	return (int)(ra->demand_tokens-rb->demand_tokens);
}

float max_min_fairness_index(long long available_tokens, struct fstb_class * classes, size_t n_class)
{
	qsort(classes, n_class, sizeof(struct fstb_class), cmp_fstb_class);
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