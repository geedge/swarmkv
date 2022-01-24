#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif


struct swarmkv_readoptions;
struct swarmkv_writeoptions;

struct swarmkv_store;

// config example: "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
// bootstraps example: "proto=udp;self=0.0.0.0:8323;peers=192.168.0.100:8323,[2001:db8::1]:8080"
struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err);
void swarmkv_close(struct swarmkv_store *store);
int swarmkv_set(struct swarmkv_store *store, const char *table, const struct swarmkv_writeoptions *options,
				const char *key, size_t keylen, const char *value, size_t vallen, char **errptr);
//Returend value MUST Be freed by caller.
char *swarmkv_get(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options,
					const char *key, size_t keylen, size_t *vallen, char **errptr);
int swarmkv_put(struct swarmkv_store *store, const char *table, const struct swarmkv_writeoptions *options, 
					const char *key, size_t keylen, const char *value, size_t vallen, char **errptr);
char *swarmkv_delete(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options,
						const char *key, size_t keylen);



typedef void swarmkv_callback_func_t(const char *table, const char *key, size_t keylen,
										const char *value, size_t vallen, void *arg);
void swarmkv_get_with_callback(struct swarmkv_store *store, const char *table, const struct swarmkv_readoptions *options, 
								const char *key, size_t keylen, swarmkv_callback_func_t *cb, void *arg);
int swarmkv_put_with_callback(struct swarmkv_store *store, const char *tb_name, const struct swarmkv_writeoptions *options,
							const char *key, size_t keylen, const char *value, size_t vallen, swarmkv_callback_func_t *cb, void *cb_arg);


#ifdef __cplusplus
} /* end extern "C" */
#endif
