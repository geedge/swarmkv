#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
	swarmkv_type_string,
	swarmkv_type_integer,
	swarmkv_type_array
};

struct swarmkv_readoptions;
struct swarmkv_writeoptions;

struct swarmkv_store;

// config example: "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
// bootstraps example: "self=0.0.0.0:8323;peers=192.168.0.100:8323,192.168.0.101:8323"
// bootstraps example: "self=0.0.0.0:8323;peers=tcp://192.168.0.100:8323,tcp://192.168.0.101:8323"
struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err);
void swarmkv_close(struct swarmkv_store *store);
int swarmkv_set(struct swarmkv_store *store, const char *table, const struct swarmkv_writeoptions *options,
				const char *key, size_t keylen, const char *value, size_t vallen, char **errptr);
//Returend value MUST Be freed by caller.
char *swarmkv_get(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options,
					const char *key, size_t keylen, size_t *vallen, char **errptr);
int swarmkv_put(struct swarmkv_store *store, const char *table, const struct swarmkv_writeoptions *options, 
					const char *key, size_t keylen, const char *value, size_t vallen, char **errptr);
typedef void swarmkv_callback_func_t(const char *table, const char *key, size_t keylen,
										const char *value, size_t vallen, void *arg);
void swarmkv_get_with_callback(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options, 
								const char *key, size_t keylen, swarmkv_callback_func_t *cb, void *arg);
int swarmkv_put_with_callback(struct swarmkv_store *store, const char *tb_name, const struct swarmkv_writeoptions *options,
							const char *key, size_t keylen, const char *value, size_t vallen, swarmkv_callback_func_t *cb, void *cb_arg);
char *swarmkv_delete(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options,
						const char *key, size_t keylen);

long long swarmkv_incrby(struct swarmkv_store *store, const char *key, const struct swarmkv_writeoptions *options, size_t key_sz, long long increment);

void swarmkv_hset(struct swarmkv_store *store, const struct swarmkv_writeoptions *options, const char *key, size_t keylen, const char *field, const char *value, size_t vallen);

//测试辅助函数
char *swarmkv_get_cache(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, size_t *vallen);
char *swarmkv_GetKeyOpt(struct swarmkv_store *store, const char *tb_name, const char *key, size_t keylen, size_t *vallen);
char *swarmkv_GetSlotAssign(struct swarmkv_store *store);
int swarmkv_cluster_open(const char *bootstraps, const char *config, char **err);
void free_store_space(struct swarmkv_store* store);
int caculate_key_slot_id(const char *key, size_t keylen);
//int swarmkv_subscribe(struct swarmkv_store *store, const char *table, cb_func callback);
//struct swarmkv_reply *swarmkv_command(struct swarmkv_store *store, const char *format, ...);
//value = swarm_kv_command(store, "hget %s %s", key, mbr);
//void swarmkv_close(struct swarmkv_store *store);

void test();

#ifdef __cplusplus
} /* end extern "C" */
#endif


//https://sysdig.com/blog/redis-prometheus/ redis统计信息
//https://git.mesalab.cn/MESA_framework/FieldStat2/-/blob/master/test/fs2_test.cpp 可用参考库

//查看当前本节点持有kv的状态，本地/缓存/没有等，不能把测试代码写到swarmkv源码里
//swarmkv_getkeyopt(kv, key, SWMKV_OPT_KEY_LOCATION, *value);
//EXPECT_EQ (value, LOCAL_KEY);