
/*
refer to A High-Performance Invertible Sketch for
Network-Wide Superspreader Detection

-----------------------
update, query, merge, reset...

query 的结果是key: count。尽量多输出。
内部用hll 实现，包括在小范围内hll 其实挺准，包括multi resolution 不能直接适配sliding window。

hll add:
    st_hll_slide_window
    +add hash

st_hll_slide_window
对于delta_us 的值，持续reset。

reset_register 
time_window_ms 和 precision 是两个参数，决定了一个register 对应多久的时间（τ）
距离上次reset 的时间差，决定了reset 的个数，reset 从上次中断的地方继续。


当update 一个位置时，就是update 这个地方的register，记录更新的时间的地方。其他地方的register不动。配置是统一的，可以提出来。

当query时：
如果没有哈希冲突，那么正常在同一个时刻更新r个hll，状态是一样的。
如果有哈希冲突，那么所有被更新的hll，其状态不同。
因为我在查询的时候，查询的是从[now-SLIDING_WINDOW, now] 的记录，但是每个bucket 里的hll 状态里保存的now 都不一样
这里要有一个callibrate 的过程，把所有的hll 都callibrate 到同一个时间点。

如果是timestamp的bitmap呢？它是无状态的 ，单纯是x 时刻，某个地方到达的数量有多少。

*/


#include <stdlib.h> 
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "spread_sketch.h"
#include "st_hyperloglog.h"


struct spread_sketch
{
    struct ST_hyperloglog *hll;
    int depth;
    int width;
    unsigned char precision;
    long long time_window_ms;
    struct timeval last_reset;
};


struct spread_sketch *spread_sketch_new(int depth, int width, // for CM sketch
                                        unsigned char precision, long long time_window_ms, const struct timeval now, // for staggered hll   
                                        )
{


}

void spread_sketch_free(struct spread_sketch *ss)
{
    ST_hyperloglog_free(ss->hll);
    free(ss);
}


//right most zero
int right_most_zero(unsigned long p) {
    // todo: 更快的方法
    // __builtin_popcountll https://git.mesalab.cn/swarmkv/swarmkv/-/blob/develop-version4/CRDT/ap_bloom.c?ref_type=heads
    int ret = 0;
    while ((p&0x00000001) == 1) {
        p >>= 1;
        ret++;
    }
    return ret+1;
}

void update(struct spread_sketch *ss, const char *src, const char *dst, const struct timeval now)
{
}

struct ss_query_result *query(struct spread_sketch *ss, const struct timeval now)
{
}

void merge(struct spread_sketch *dst, const struct spread_sketch *src)
{
}

void serialize(struct spread_sketch *ss, char **blob, size_t *blob_sz)
{
}

struct spread_sketch *deserialize(const char *blob, size_t blob_sz)
{
}

void spread_sketch_merge_blob(struct spread_sketch *dst, const char *blob, size_t blob_sz)
{
}

