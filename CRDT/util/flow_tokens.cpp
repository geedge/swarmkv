#include "flow_tokens.h"
#include "math.h"
#include "../crdt_utils.h"

long long get_request_tokens(long long class_num, long long CIR, size_t index, long long step_us, enum traffic_type type)
{
    long long request_size;
    double average_ratio2, average_ratio8, total_ratio2, total_ratio8;
    long long standard = CIR*step_us/1000;
    // long long sdf = floor((long double)standard * 0.05);	//floating 5%
    size_t occupy8_replica_num = floor(class_num * 0.8);
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
            average_ratio2 = total_ratio2 / occupy8_replica_num;
            average_ratio8 = total_ratio8 / (class_num-occupy8_replica_num);
            if (index < occupy8_replica_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) + rand_sdf;
            } else if (index < occupy8_replica_num && !scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) - rand_sdf;
            } else if (index >= occupy8_replica_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio8) + rand_sdf;
            } else {
                request_size = (long long)floor((long double)standard * average_ratio8) - rand_sdf;
            }
            break;
        case HEAVY_TWO_EIGHT_TYPE:
            //average 2*CIR per node
            total_ratio2 = (2.0*class_num) / 5;
            total_ratio8 = (2.0*class_num) - total_ratio2;
            average_ratio2 = total_ratio2 / occupy8_replica_num;
            average_ratio8 = total_ratio8 / (class_num-occupy8_replica_num);
            if (index < occupy8_replica_num && scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) + rand_sdf;
            } else if (index < occupy8_replica_num && !scope_flag) {
                request_size = (long long)floor((long double)standard * average_ratio2) - rand_sdf;
            } else if (index >= occupy8_replica_num && scope_flag) {
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

void set_flow_prob(double *prob, int replica_num, enum flow_type ftype)
{
    int i;
    if(ftype == UNIFORM)
    {
        for(i = 0; i < replica_num; i++)
        {
            prob[i] = 1;
        }
    }else{
        int eight_replica_num = floor(replica_num * 0.8);
        for(i = 0; i < eight_replica_num; ++i)
        {
            prob[i] = ceil(1000*2.0/eight_replica_num);
        }
        for(; i < replica_num; ++i)
        {
            prob[i] = floor(1000*8.0/(replica_num-eight_replica_num));
        }
    }
}