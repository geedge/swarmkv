#include "crdt_utils.h"
#include "st_hyperloglog.h"
#include "spread_sketch.h"
#include "hll_common.h"

#include <gtest/gtest.h>
#include <math.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <random>
#include <fstream>


struct timeval ms_to_timeval(long long ms)
{
    struct timeval tv;
    tv.tv_sec=ms/1000;
    tv.tv_usec=(ms%1000)*1000;
    return tv;
}

struct Flow {
    std::string src_ip;
    std::string dst_ip;
    long long time;
};

class InputGenerator {
private:
    double _now;
    int _n_super_spreader;
    int _n_others;
    int _fanout_superspreader;
    int _fanout_others;
    double _probability_ss;

    double _ms_per_packet;

public:
    InputGenerator(int n_super_spreader, int n_others, double probability_ss, int fanout_superspreader, int fanout_others, long long now, double ms_per_packet ) {
        _now = (double)now;
        _n_super_spreader = n_super_spreader;
        _n_others = n_others;
        _probability_ss = probability_ss;

        _fanout_superspreader = fanout_superspreader;
        _fanout_others = fanout_others;
        _ms_per_packet = ms_per_packet;

        if (_n_super_spreader == 0) {
            _probability_ss = 0;
        }
        if (_n_others == 0) {
            _probability_ss = 1;
        }
    }

    struct Flow next() {
        struct Flow flow;
        if (rand() % 100 < _probability_ss * 100 && _n_super_spreader > 0) {
            flow.src_ip = "s_" + std::to_string(rand() % _n_super_spreader);
            flow.dst_ip = "sd_" + std::to_string(rand() % _fanout_superspreader);
        } else {
            flow.src_ip = std::to_string(rand() % _n_others);
            flow.dst_ip = "d_" + std::to_string(rand() % _fanout_others);
        }
        flow.time = (long long)_now;
        _now += _ms_per_packet;
        return flow;
    }
};

struct spread_sketch_list {
    char **key;
    size_t *key_len;
    double *count;
    int n_results;
};

struct spread_sketch_list *spread_sketch_list(const struct spread_sketch *ss) {
    size_t n_result;
    size_t *key_lens;
    char **keys;
    spread_sketch_list_keys(ss, &keys, &key_lens, &n_result);

    struct spread_sketch_list *ret = (struct spread_sketch_list *)malloc(sizeof(struct spread_sketch_list));
    if (n_result == 0) {
        ret->n_results = 0;
        ret->key = NULL;
        ret->count = NULL;
        return ret;
    }

    // sort by count
    std::vector<std::pair<double, size_t>> sorted;
    for (size_t i = 0; i < n_result; i++) {
        sorted.push_back(std::make_pair(spread_sketch_get_cardinality(ss, keys[i], key_lens[i]), i));
    }
    std::sort(sorted.begin(), sorted.end(), [](const std::pair<double, size_t> &a, const std::pair<double, size_t> &b) {
        return a.first > b.first;
    });

    ret->key = (char **)malloc(sizeof(char *) * n_result);
    ret->count = (double *)malloc(sizeof(double) * n_result);
    ret->key_len = (size_t *)malloc(sizeof(size_t) * n_result);

    for (size_t i = 0; i < n_result; i++) {
        size_t idx = sorted[i].second;
        ret->key[i] = (char *)malloc(key_lens[idx]);
        memcpy(ret->key[i], keys[idx], key_lens[idx]);
        ret->key_len[i] = key_lens[idx];
        ret->count[i] = sorted[i].first;
    }

    ret->n_results = n_result;
    free(keys);
    free(key_lens);
    return ret;
}

void ss_query_result_free(struct spread_sketch_list *result) {
    if (result == NULL) {
        return;
    }

    for(int i=0; i<result->n_results; i++){
        free(result->key[i]);
    }
    free(result->key);
    free(result->count);
    free(result->key_len);
    free(result);
}

class TestRet {
private:
    struct spread_sketch_list *ret_raw;

    unsigned char _precision;
    long long _window_s;

    long long _last_time;
    bool _use_hll;


public:
    TestRet(){
        ret_raw = NULL;
        _precision = 4;
        _window_s = 10;
        _use_hll = false;
    }

    TestRet(unsigned char precision, long long window_s) {
        ret_raw = NULL;
        _precision = precision;
        _window_s = window_s;
        _use_hll = true;
    }

    ~TestRet(){
        ss_query_result_free(ret_raw);
        if (_use_hll) {
            for (auto &kv : benchmark_sthll) {
                ST_hyperloglog_free(kv.second);
            }
        }
    }

    void feed(std::string key_main, std::string key_spread, long long now_ms){
        benchmark[key_main].insert(key_spread);

        if (_use_hll) {
            if (benchmark_sthll.find(key_main) == benchmark_sthll.end()) {
                benchmark_sthll[key_main] = ST_hyperloglog_new(_precision, _window_s * 1000, ms_to_timeval(now_ms));
            }
            
            ST_hyperloglog_add(benchmark_sthll[key_main], key_spread.c_str(), key_spread.size(), ms_to_timeval(now_ms));
        }

        _last_time = now_ms;
    }

    void read_ss_query_result(struct spread_sketch_list *ret){
        this->ret_raw = ret;

        // printf("read_ss_query_result n_results: %d\n", ret->n_results);
        for (int i = 0; i < ret->n_results; i++) {
            std::string key(ret->key[i], ret->key_len[i]);
            // struct ST_hyperloglog *hll = _use_hll ? benchmark_sthll[key] : NULL;
            // double hllcnt = hll == NULL ? -1.0 : ST_hyperloglog_count(hll, ms_to_timeval(_last_time));
            // long actural = benchmark[key].size();
            // printf("key: %s, count: %f, hll benchmark: %f, actual: %lu\n", key.c_str(), ret->count[i], hllcnt, actural);

            results[key] = ret->count[i];
        }
    }

    double cal_mre(){
        double mre = 0;
        int cnt = 0;
        for (auto &kv : benchmark) {
            // only calculate MRE for keys that exist in benchmark
            // and only take account of spread keys(start with s_)
            if (kv.first[0] != 's') {
                continue;
            }
            double est = results.find(kv.first) == results.end() ? 0 : results[kv.first];
            double actual = kv.second.size();

            mre += fabs(est - actual) / actual;
            cnt++;
        }
        return mre / cnt;
    }

    double cal_mre(double top_percent){
        double mre = 0;
        std::vector<std::pair<std::string, int>> top_n_keys;
        for (auto &kv : benchmark) {
            int fanout = kv.second.size();
            top_n_keys.push_back(std::make_pair(kv.first, fanout));
        }
        std::sort(top_n_keys.begin(), top_n_keys.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
            return a.second > b.second;
        });

        int total_ss = top_percent * top_n_keys.size();
        for (int i = 0; i < total_ss; i++) {

            double est = results.find(top_n_keys[i].first) == results.end() ? 0 : results[top_n_keys[i].first];
            double actual = top_n_keys[i].second;

            mre += fabs(est - actual) / actual;
        }
        return mre / total_ss;
    }

    bool check_result(unsigned char hll_precision, double eplilon, double delta) {
        int good = 0;
        int total_valid_key = 0;
        double sigma = hll_error(hll_precision) * 2; // RSD -> max error estimation

        for (auto &kv : benchmark) {
            if (results.find(kv.first) == results.end()) {
                continue;
            }

            double actual = kv.second.size();
            double est = results[kv.first];
            total_valid_key++;
 
            // sigma is actually an RSD instead of an error estimation, but the chances of error estimation being larger than RSD is low enough to ignore, compared to the error of CM sketch
           // est is always bigger than actual if HLL error is omitted
            if (est <= (1+sigma) * (1+eplilon) * actual && est >= (1-sigma) * actual) {
                good++;
            }
        }
        
        bool ret = good >= total_valid_key * (1 - delta);
        if (!ret) {
        // if (true) {
            printf("test failed, good: %d, total: %d\n", good, total_valid_key);
            printf("allowed error percentage: %f\n", delta);
            // print out the whole benchmark and results
            for (auto &kv : benchmark) {
                if (results.find(kv.first) == results.end()) {
                    continue;
                }

                if (_use_hll) {
                    printf("key: %s, actual: %lu, estimated: %f, allowed range [%f, %f], hll result: %f\n", 
                        kv.first.c_str(),
                        kv.second.size(),
                        results[kv.first], (1-sigma) * kv.second.size(),
                        (1+sigma) * (1+eplilon) * kv.second.size(),
                        ST_hyperloglog_count(benchmark_sthll[kv.first], ms_to_timeval(_last_time)));
                } else {
                    printf("key: %s, actual: %lu, estimated: %f, allowed range [%f, %f]\n", 
                        kv.first.c_str(),
                        kv.second.size(),
                        results[kv.first], (1-sigma) * kv.second.size(),
                        (1+sigma) * (1+eplilon) * kv.second.size());
                }
            }
        }
        return ret;
    }

    double cal_recall_rate(int top_n) {
        std::vector<std::pair<std::string, int>> top_n_keys;
        for (auto &kv : benchmark) {
            int fanout = kv.second.size();
            top_n_keys.push_back(std::make_pair(kv.first, fanout));
        }
        std::sort(top_n_keys.begin(), top_n_keys.end(), [](const std::pair<std::string, int> &a, const std::pair<std::string, int> &b) {
            return a.second > b.second;
        });

        int good = 0;
        int total_ss = MIN(top_n, (int)top_n_keys.size());
        for (int i = 0; i < total_ss; i++) {
            if (results.find(top_n_keys[i].first) != results.end()) {
                good++;
            }
        }

        return 1.0 * good / total_ss;
    }

    void print() {
        for (auto &kv : benchmark) {
            double hll_result = _use_hll ? ST_hyperloglog_count(benchmark_sthll[kv.first], ms_to_timeval(_last_time)) : -1.0;
            printf("key: %s, benchmark size: %lu, estimated size: %f, hll result: %f\n",
             kv.first.c_str(), kv.second.size(), results.find(kv.first) == results.end() ? -1.0 : results[kv.first], hll_result);
        }

        for (auto &kv : results) {
            if (benchmark.find(kv.first) == benchmark.end()) {
                printf("key not existed in benchmark:: %s, estimated size: %f\n", kv.first.c_str(), kv.second);
            }
        }
    }

    bool key_exist(std::string key) {
        return results.find(key) != results.end();
    }

    std::unordered_map<std::string, std::unordered_set<std::string>> benchmark;
    std::unordered_map<std::string, double> results;
    std::unordered_map<std::string, struct ST_hyperloglog *> benchmark_sthll;
};

TestRet *spread_sketch_test(struct spread_sketch *ss, long long window_s, long long duration_s, class InputGenerator &gen)
{
    long long duration_ms = duration_s * 1000;
    long long last_window_start_ms = duration_ms - window_s * 1000;

    TestRet *test_ret = new TestRet(4, window_s);

    struct Flow flow = gen.next();
    long long start_ms = flow.time;
    while (flow.time - start_ms <= duration_ms) {
        std::string src_ip = flow.src_ip;
        std::string dst_ip = flow.dst_ip;
        long long now = flow.time;
        spread_sketch_add(ss, src_ip.c_str(), src_ip.size(), dst_ip.c_str(), dst_ip.size(), NULL, ms_to_timeval(now));
        if (now >= last_window_start_ms) {
            test_ret->feed(src_ip, dst_ip, now);
        }

        flow = gen.next();
    }

    struct spread_sketch_list *results = spread_sketch_list(ss);
    test_ret->read_ss_query_result(results);

    return test_ret;
}

TestRet *spread_sketch_generic_test(int precision, int depth, int width, long long window_s, long long duration_s, int n_super_spreader, int n_total, int fanout_superspreader, int fanout_others) 
{
    long long now = 0;
    const int N_INSTANCES = 10;

    struct spread_sketch *ss_instances[N_INSTANCES];
    for (int i = 0; i < N_INSTANCES; i++) {
        ss_instances[i] = spread_sketch_new(depth, width, precision, window_s * 1000, ms_to_timeval(now));
    }

    double ms_per_packet = 1.0 * (window_s * 1000) / (fanout_superspreader * n_super_spreader + fanout_others * (n_total - n_super_spreader));
    class InputGenerator gen(n_super_spreader, n_total - n_super_spreader, 0.5, fanout_superspreader, fanout_others, now, ms_per_packet / 1.0);
    long long duration_ms = duration_s * 1000;
    long long last_window_start_ms = duration_ms - window_s * 1000;
    TestRet *test_ret = new TestRet(precision, window_s);

    struct Flow flow = gen.next();
    long long start_ms = flow.time;
    while (flow.time - start_ms <= duration_ms) {
        std::string src_ip = flow.src_ip;
        std::string dst_ip = flow.dst_ip;
        long long now = flow.time;

        struct spread_sketch *ss = ss_instances[rand() % N_INSTANCES];
        spread_sketch_add(ss, src_ip.c_str(), src_ip.size(), dst_ip.c_str(), dst_ip.size(), NULL, ms_to_timeval(now));
        if (now >= last_window_start_ms) {
            test_ret->feed(src_ip, dst_ip, now);
        }

        flow = gen.next();
    }

    struct spread_sketch *ss = ss_instances[0];
    for (int i = 1; i < N_INSTANCES; i++) {
        spread_sketch_merge(ss, ss_instances[i]);
    }

    struct spread_sketch_list *results = spread_sketch_list(ss);
    test_ret->read_ss_query_result(results);

    for (int i = 0; i < N_INSTANCES; i++) {
        spread_sketch_free(ss_instances[i]);
    }

    return test_ret;
}


double cal_CM_delta(int depth) {
    return 1.0 / (1 << depth);
}

double cal_CM_epsilon(int width) {
    return 2.0 / width;
}

TEST(SpreadSketch, OneEntry)
{
    TestRet *test_ret = spread_sketch_generic_test(
        4, // hll precision
        4, 1024, // depth, width
        10, 100, // window, duration
        0, 1, // n_super_spreader, n_total. just one flow, so no error on CM sketch
        10000, 10000); // fanout_superspreader, fanout_others

    bool good = test_ret->check_result(4, 0, 0);
    EXPECT_TRUE(good);

    delete test_ret;
}

TEST(SpreadSketch, OneEntryNoSliding)
{
    // use very little fp compared to big sketch so that CM sketch error are negligible
    TestRet *test_ret = spread_sketch_generic_test(
        4, // hll precision
        4, 1024, // depth, width
        10, 1, // window, duration, window is larger than duration
        0, 1, // n_super_spreader, n_total. just one flow, so no error on CM sketch
        10000, 10000); // fanout_superspreader, fanout_others
    
    bool good = test_ret->check_result(4, 0, 0);
    EXPECT_TRUE(good);

    delete test_ret;
}

TEST(SpreadSketch, Precision)
{
    TestRet *test_ret = spread_sketch_generic_test(
        6, // hll precision
        4, 1024, // depth, width
        100, 200, // window, duration
        500, 10000, // n_super_spreader, n_total.
        1000, 3); // fanout_superspreader, fanout_others
    
    double mre = test_ret->cal_mre();
    EXPECT_LE(mre, 0.4);

    bool good = test_ret->check_result(6, cal_CM_epsilon(1024), cal_CM_delta(4));
    EXPECT_TRUE(good);

    double recall_rate = test_ret->cal_recall_rate(500); // 500: n_super_spreader
    EXPECT_GE(recall_rate, 0.85);

    // printf("normal_flows recall rate: %f, mre: %f \n", recall_rate, mre);

    delete test_ret;
}

TEST(SpreadSketch, PrecisionOfScarceEntry)
{
    int window_s = 100;
    int n_super_spreader = 100;
    struct spread_sketch *ss = spread_sketch_new(5, 1024, 4, window_s * 1000, ms_to_timeval(0));
    InputGenerator gen(n_super_spreader, 1000, 0.5, 1000, 3, 0, 100);

    TestRet *test_ret = spread_sketch_test(ss, window_s, window_s * 5, gen);
    
    double mre = test_ret->cal_mre();
    EXPECT_LE(mre, 0.4);

    // not a typical case, so STHLL accuracy is not guaranteed

    double recall_rate = test_ret->cal_recall_rate(n_super_spreader);
    EXPECT_GE(recall_rate, 0.9);

    // printf("scarce_flow recall rate: %f, mre: %f \n", recall_rate, mre);

    delete test_ret;
    spread_sketch_free(ss);
}

TEST(SpreadSketch, Expire) {
    int n_super_spreader = 1000;
    int window_s = 10;

    struct spread_sketch *ss = spread_sketch_new(4, 1024, 6, window_s * 1000, ms_to_timeval(0));
    long long now = 0;

    for (int i = 0; i < n_super_spreader; i++) {
        std::string src = "old_" + std::to_string(i);

        for (int k = 0; k < 1000; k++) {
            std::string dst = "d_" + std::to_string(k);
            spread_sketch_add(ss, src.c_str(), src.size(), dst.c_str(), dst.size(), NULL, ms_to_timeval(now));
            now += 5;
        }
    }
    InputGenerator gen(n_super_spreader, n_super_spreader * 9, 0.5, 1000, 3, now, 0.01);
    TestRet *test_ret = spread_sketch_test(ss, window_s, window_s * 3, gen);
    // all old keys should be decayed
    for (int i = 0; i < n_super_spreader; i++) {
        EXPECT_FALSE(test_ret->key_exist("old_" + std::to_string(i)));
    }

    double recall_rate = test_ret->cal_recall_rate(n_super_spreader);
    EXPECT_GE(recall_rate, 0.9);
    // printf("decay_old_key_not_included recall rate: %f\n", recall_rate);

    delete test_ret;
    spread_sketch_free(ss);
}

TEST(SpreadSketch, Serialize) {
    struct spread_sketch *ss = spread_sketch_new(4, 1024, 4, 10*1000, ms_to_timeval(0));
    int key1 = 1;
    int key2 = 2;
    int dstKey1 = 11;
    int dstKey2 = 22;

    spread_sketch_add(ss, (char *)&key1, sizeof(key1), (char *)&dstKey1, sizeof(dstKey1), NULL, ms_to_timeval(0));
    spread_sketch_add(ss, (char *)&key1, sizeof(key1), (char *)&dstKey2, sizeof(dstKey2), NULL, ms_to_timeval(10));
    spread_sketch_add(ss, (char *)&key2, sizeof(key2), (char *)&dstKey1, sizeof(dstKey1), NULL, ms_to_timeval(20));

    char *blob;
    size_t blob_sz;
    spread_sketch_serialize(ss, &blob, &blob_sz);

    struct spread_sketch *ss_deserialized = spread_sketch_deserialize(blob, blob_sz);

    struct spread_sketch_list *result = spread_sketch_list(ss);
    struct spread_sketch_list *result_deserialized = spread_sketch_list(ss_deserialized);

    EXPECT_EQ(result->n_results, result_deserialized->n_results);
    for (int i = 0; i < result->n_results; i++) {
        EXPECT_EQ(result->key_len[i], result_deserialized->key_len[i]);
        EXPECT_TRUE(memcmp(result->key[i], result_deserialized->key[i], result->key_len[i]) == 0);
        EXPECT_EQ(result->count[i], result_deserialized->count[i]);
    }

    ss_query_result_free(result);
    ss_query_result_free(result_deserialized);
    spread_sketch_free(ss);
    spread_sketch_free(ss_deserialized);
    free(blob);
}

void spread_sketch_parameter_experiments(int depth, int width, int precision, long long window_s, int n_super_spreader, int fanout_superspreader) {
    struct spread_sketch *ss = spread_sketch_new(depth, width, precision, window_s * 1000, ms_to_timeval(0));
    InputGenerator gen(n_super_spreader, n_super_spreader * 9, 0.5, fanout_superspreader, 3, 0, 1);    
    TestRet *test_ret = spread_sketch_test(ss, window_s, window_s * 5, gen);

    double mre = test_ret->cal_mre();
    double recall_rate = test_ret->cal_recall_rate(n_super_spreader);
    size_t memory_cost = spread_sketch_calculate_memory_usage(ss);
    spread_sketch_free(ss);

    delete test_ret;

    printf("depth: %d, width: %d, precision: %d, window: %lld, n_super_spreader: %d, fanout_superspreader: %d. \n\trecall rate: %f, mre: %f, memory cost: %fkb\n",
        depth, width, precision, window_s, n_super_spreader, fanout_superspreader, recall_rate, mre, memory_cost / 1024.0);
    printf("===============================================\n");
}

TEST(SpreadSketchExperiments, UniformDistribution) {
    long long window_s = 10;
    // check CM sketch related parameters
    int depth[] = {3, 4};
    int width[] = {64, 128, 256, 512, 1024, 2048};
    int n_super_spreader[] = {10, 100, 1000};

    for (size_t i = 0; i < sizeof(depth) / sizeof(int); i++) {
        for (size_t j = 0; j < sizeof(width) / sizeof(int); j++) {
            for (size_t l = 0; l < sizeof(n_super_spreader) / sizeof(int); l++) {
                    spread_sketch_parameter_experiments(depth[i], width[j], 4, window_s, n_super_spreader[l], 500);
            }
        }
    }

    // HLL related parameters
    int precision[] = {4,5,6,7};
    int fanouts[] = {10, 100, 1000, 10000};
    for (size_t i = 0; i < sizeof(precision) / sizeof(int); i++) {
        for (size_t j = 0; j < sizeof(fanouts) / sizeof(int); j++) {
            spread_sketch_parameter_experiments(4, 1024, precision[i], window_s, 100, fanouts[j]);
        }
    }

    // window 
    long long windows[] = {5, 10, 20, 30, 60};
    for (size_t i = 0; i < sizeof(windows) / sizeof(long long); i++) {
        spread_sketch_parameter_experiments(3, 256, 5, windows[i], 100, 500);
    }
}


//===========================================================================
//=  Function to generate Zipf (power law) distributed random variables     =
//=    - Input: alpha and N                                                 =
//=    - Output: Returns with Zipf distributed random variable              =
//===========================================================================
int zipf(double alpha, int n)
{
  static bool first = true;      // Static first time flag
  static double c = 0;          // Normalization constant
  double z;                     // Uniform random number (0 < z < 1)
  double sum_prob;              // Sum of probabilities
  double zipf_value;            // Computed exponential value to be returned
  int    i;                     // Loop counter

  // Compute normalization constant on first call only
  if (first)
  {
    for (i=1; i<=n; i++)
      c = c + (1.0 / pow((double) i, alpha));
    c = 1.0 / c;
    first = false;
  }

  // Pull a uniform random number (0 < z < 1)
  do
  {
    z = (double)rand() / (double)RAND_MAX;
  }
  while ((z == 0.0) || (z == 1.0));

  // Map z to the value
  sum_prob = 0;
  for (i=1; i<=n; i++)
  {
    sum_prob = sum_prob + c / pow((double) i, alpha);
    if (sum_prob >= z)
    {
      zipf_value = i;
      break;
    }
  }

  return(zipf_value);
}

class ZipfInputGenerator {
private:
    double _now;
    double _ms_per_packet;

    const int MAX_DATA = 1000000;
    std::pair<std::string, std::string> *loadeds;
    unsigned cursor;

public:
    ZipfInputGenerator(double alpha, int n, long long now, double ms_per_packet) {
        _now = now;
        _ms_per_packet = ms_per_packet;
        _alpha = alpha;
        _n = n;
        
        // generate data and write them to file
        std::string filename = "zipf_" + std::to_string(alpha) + "_" + std::to_string(n) + ".txt";

        std::unordered_map<int, int> fanout_map; // src_ip_id -> fanout being used

        if (access(filename.c_str(), F_OK) != 0) {
            printf("file %s not found, generating data\n", filename.c_str());

            std::ofstream file(filename);
            if (!file.is_open()) {
                printf("failed to open file %s\n", filename.c_str());
                return;
            }
            
            for (int i = 0; i < MAX_DATA; i++) {
                int src_id = zipf(alpha, n);
                int fanout = fanout_map.find(src_id) == fanout_map.end() ? 0 : fanout_map[src_id];
                fanout_map[src_id] = fanout + 1;

                file << "s_" << src_id << " d_" << fanout << std::endl;
            }
            
            file.close();
            printf("data generated and saved to file %s\n", filename.c_str());
        }

        // load data
        std::ifstream file(filename);
        if (!file.is_open()) {
            printf("failed to open file %s\n", filename.c_str());
            return;
        }

        loadeds = new std::pair<std::string, std::string>[MAX_DATA];
        std::string line;
        int i = 0;
        while (std::getline(file, line) && i < MAX_DATA) {
            std::istringstream iss(line);
            std::string src_ip, dst_ip;
            iss >> src_ip >> dst_ip;
            loadeds[i] = std::make_pair(src_ip, dst_ip);
            i++;
        }
        file.close();
    }

    struct Flow next() {
        int r_cursor = cursor % MAX_DATA;
        struct Flow flow;
        flow.src_ip = loadeds[r_cursor].first;
        flow.dst_ip = loadeds[r_cursor].second;
        flow.time = _now;

        _now += _ms_per_packet;
        cursor++;

        return flow;
    }

    ~ZipfInputGenerator() {
        delete[] loadeds;
    }

    double _alpha;
    int _n;
};

void parameter_experiments_given_zipf_experiment(int depth, int width, int precision, long long window_s, ZipfInputGenerator &input) {
    struct spread_sketch *ss = spread_sketch_new(depth, width, precision, window_s * 1000, ms_to_timeval(0));
    TestRet *ret = new TestRet();

    Flow flow = input.next();
    long long start_ms = flow.time;
    long long duration_ms = 1000 * window_s * 5;
    long long last_window = duration_ms - window_s * 1000;

    while (flow.time - start_ms <= duration_ms) {
        spread_sketch_add(ss, flow.src_ip.c_str(), flow.src_ip.size(), flow.dst_ip.c_str(), flow.dst_ip.size(), NULL, ms_to_timeval(flow.time));
        flow = input.next();

        if (flow.time >= last_window) {
            ret->feed(flow.src_ip, flow.dst_ip, flow.time);
        }
    }

    struct spread_sketch_list *results = spread_sketch_list(ss);
    ret->read_ss_query_result(results);

    double mre = ret->cal_mre();
    double mre_reach_top_10 = ret->cal_mre(0.1);
    double recall_rate = ret->cal_recall_rate(input._n / 10); // int n_src_ip = 10 * n_superspreader

    printf("depth: %d, width: %d, precision: %d, window: %lld. Skewness: %f n_src: %d.\n\tmre: %f, recall: %f, top_mre: %f\n",
           depth,     width,          precision,    window_s,    input._alpha,   input._n,     mre,     recall_rate,     mre_reach_top_10);

    delete ret;
    spread_sketch_free(ss);
}

TEST(SpreadSketchExperiments, ZipfDistribution) {
    /*
    test with flows in zipf distribution fanout. 
    Every time window, there are `n_flows_per_window` distinct flows generated. Cardinality({y|x=x_i}) ~ Zipf(N, skewness), where N = 10 * n_superspreader
    */
    long long window_s = 10;
    // check CM sketch related parameters
    int depth[] = {3,4};
    int width[] = {64,128,256,512,1024};
    double skewness[] = {0.3,0.7,1.0};
    int n_superspreader[] = {10,100,1000};

    for (size_t i = 0; i < sizeof(depth) / sizeof(int); i++) {
        for (size_t j = 0; j < sizeof(width) / sizeof(int); j++) {
            for (size_t l = 0; l < sizeof(skewness) / sizeof(double); l++) {
                for (size_t k = 0; k < sizeof(n_superspreader) / sizeof(int); k++) {
                    int n_src_ip = 10 * n_superspreader[k];
                    int n_flows_per_window = n_src_ip * 100; // 10 * 1000 * 100 = 1e6
                    double ms_per_packet = 1.0 * window_s * 1000 / n_flows_per_window;

                    ZipfInputGenerator input(skewness[l], n_src_ip, 0, ms_per_packet);
                    parameter_experiments_given_zipf_experiment(depth[i], width[j], 4, window_s, input);
                }
            }
        }
    }

    // // HLL related parameters
    int precision[] = {4,5,6,7};
    for (size_t i = 0; i < sizeof(precision) / sizeof(int); i++) {
        for (size_t j = 0; j < sizeof(skewness) / sizeof(double); j++) {
            for (size_t k = 0; k < sizeof(n_superspreader) / sizeof(int); k++) {
                int n_src_ip = 10 * n_superspreader[k];
                int n_flows_per_window = n_src_ip * 100; // 10 * 1000 * 100 = 1e6
                double ms_per_packet = 1.0 * window_s * 1000 / n_flows_per_window;

                ZipfInputGenerator input(skewness[j], n_src_ip, 0, ms_per_packet);
                parameter_experiments_given_zipf_experiment(4, 1024, precision[i], window_s, input);
            }
        }
    }

    // after deciding precision = 6, and width = K, check which depth is better.
    int depth2[] = {3, 4};
    int width2_10[] = {60};
    int width2_100[] = {100,150,200,250,300,350,400};
    int width2_1000[] = {800,900,1000,1100,1200,1300,1400,1500};
    int *width2[3] = {width2_10, width2_100, width2_1000};
    size_t n_w_choice[3] = {sizeof(width2_10) / sizeof(int), sizeof(width2_100) / sizeof(int), sizeof(width2_1000) / sizeof(int)};

    for (size_t i = 0; i < sizeof(depth2) / sizeof(int); i++) {
        for (size_t j = 0; j < sizeof(n_superspreader) / sizeof(int); j++) {
            int *widths_tmp = width2[j];

            for (size_t k = 0; k < n_w_choice[j]; k++) {
                int n_src_ip = 10 * n_superspreader[j];
                int n_flows_per_window = n_src_ip * 100;
                double ms_per_packet = 1.0 * window_s * 1000 / n_flows_per_window;

                ZipfInputGenerator input(0.7, n_src_ip, 0, ms_per_packet);
                parameter_experiments_given_zipf_experiment(depth2[i], widths_tmp[k], 6, window_s, input);
            }
        }
    }

    // verify the strategy of parameter setting
    int n_superspreader2[] = {10, 20, 50,100, 150, 200,250,300,350,400,450,500,600,700,800,900};
    for (size_t i = 0; i < sizeof(n_superspreader2) / sizeof(int); i++) {
        int n_src_ip = 10 * n_superspreader2[i];
        int n_flows_per_window = n_src_ip * 500;
        double ms_per_packet = 1.0 * window_s * 1000 / n_flows_per_window;

        ZipfInputGenerator input(1, n_src_ip, 0, ms_per_packet);
        int width;
        if (n_superspreader2[i] <= 100) {
            width = n_superspreader2[i] * 3 / 2;
        } else {
            width = n_superspreader2[i] + 50;
        }
        if (width < 32) {
            width = 32;
        }

        parameter_experiments_given_zipf_experiment(3, width, 6, window_s, input);
    }
}

int main(int argc, char ** argv)
{
	int ret=0;
	::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) = "-SpreadSketchExperiments.*"; // Exclude SpreadSketchExperiments tests
    // ::testing::GTEST_FLAG(filter) = "*serialize_with_nonstring_keys";

	ret=RUN_ALL_TESTS();
	return ret;
}
