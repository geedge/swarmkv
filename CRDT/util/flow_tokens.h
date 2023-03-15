#include <stddef.h>
//请求量不等
enum traffic_type
{
    LIGHT_UNIFORM_TYPE,
    LIGHT_TWO_EIGHT_TYPE,
    HEAVY_TWO_EIGHT_TYPE,
    HEAVY_UNIFORM_EXTREME_TYPE
};

//请求量相等
enum flow_type
{
    UNIFORM,
    PARETO
};

long long get_request_tokens(long long class_num, long long CIR, size_t index, long long step_us, enum traffic_type type);

void set_flow_prob(double *prob, int replica_num, enum flow_type ftype);
