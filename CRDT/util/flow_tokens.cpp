#include "flow_tokens.h"
#include "math.h"
#include "../crdt_utils.h"
#include <cstdio>

long long get_request_tokens(long long class_num, long long CIR, size_t index, long long step_us, enum traffic_type type)
{
    long long request_size;
    double average_ratio2, average_ratio8, total_ratio2, total_ratio8;
    long long standard = CIR*step_us/1000;
    // long long sdf = floor((long double)standard * 0.05);	//floating 5%
    size_t occupy8_class_num = floor(class_num * 0.8);
//    long long rand_sdf = random()%sdf;
    long long rand_sdf = 0;
    size_t scope_flag = FALSE;
    if(random()%2) scope_flag = TRUE;
    switch (type)
    {
        case LIGHT_UNIFORM_TYPE:
            request_size = (long long)floor((long double)standard * 0.5);
            if (scope_flag) {
                request_size += rand_sdf;
            } else {
                request_size -= rand_sdf;
            }
            break;
        case LIGHT_TWO_EIGHT_TYPE:
            //average 0.67*CIR per node
            total_ratio2 = (0.67*class_num) / 5;
            total_ratio8 = (0.67*class_num) - total_ratio2;
            average_ratio2 = total_ratio2 / occupy8_class_num;
            average_ratio8 = total_ratio8 / (class_num-occupy8_class_num);
            if (index < occupy8_class_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) + rand_sdf;
            } else if (index < occupy8_class_num && !scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) - rand_sdf;
            } else if (index >= occupy8_class_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio8) + rand_sdf;
            } else {
                request_size = (long long)floor((long double)standard * average_ratio8) - rand_sdf;
            }
            break;
        case HEAVY_TWO_EIGHT_TYPE:
            //average 2*CIR per node
            total_ratio2 = (2.0*class_num) / 5;
            total_ratio8 = (2.0*class_num) - total_ratio2;
            average_ratio2 = total_ratio2 / occupy8_class_num;
            average_ratio8 = total_ratio8 / (class_num-occupy8_class_num);
            if (index < occupy8_class_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) + rand_sdf;
            } else if (index < occupy8_class_num && !scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) - rand_sdf;
            } else if (index >= occupy8_class_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio8) + rand_sdf;
            } else {
                request_size = (long long)floor((long double)standard * average_ratio8) - rand_sdf;
            }
            break;
        case HEAVY_UNIFORM_EXTREME_TYPE:
            request_size = (long long)floor((long double)standard * 2);
            if (scope_flag) {
                request_size += rand_sdf;
            } else {
                request_size -= rand_sdf;
            }
            break;
        default:
            break;
    }
    return request_size;
}

double set_flow_prob(double *prob, int class_num, enum flow_type ftype)
{
    int i;
    double sum_w = 0.0, sum_p = 0.0;
    double probabilities[class_num];
    double a = 0.9; //Pareto形参
    if(ftype == UNIFORM)
    {
        for(i = 0; i < class_num; i++)
        {
            prob[i] = 2.0/class_num;
            sum_w += prob[i];
        }
    }else if(ftype == PARETO){
        for (i = 1; i <= class_num; ++i) 
        {
            probabilities[i - 1] = (a * pow(1, a)) / pow(i, a + 1);
            sum_p += probabilities[i - 1];
        }

        for (i = 0; i < class_num; ++i) {
            probabilities[i] /= sum_p;
            prob[class_num-i-1] = probabilities[i] * 2.0;
            // printf("[%d]=%lf\n ", class_num-i-1, prob[class_num-i-1]);
            sum_w += prob[class_num-i-1];
        }
    }else if(ftype == ONEHEAVY){
        for(i = 0; i < class_num-1; i++)
        {
            prob[i] = 1.0/class_num;
            sum_w += prob[i];
        }
        prob[i] = 0.5;
        sum_w += prob[i];
    }else{
        sum_w = 0;
    }
    return sum_w;
}