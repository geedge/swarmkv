#pragma once

#ifdef __cplusplus
extern "C" {
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

// config example: "token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
struct swarmkv_store* swarmkv_open(const char* bootstrap_address, const char* config, char* err);
int swarmkv_set(struct swarmkv_store* store, const char* table, const struct swarmkv_writeoptions* options, 
					const char* key, size_t keylen, const char* value, size_t vallen, char** errptr);
//Returend value MUST Be freed by caller.
char* swarmkv_get(struct swarmkv_store*store, const char* table, const struct swarmkv_readoptions* options, 
					const char* key, size_t keylen, size_t *vallen, char** errptr);
char* swarmkv_delete(struct swarmkv_store*store, const char* table, const struct swarmkv_readoptions* options,
					const char* key, size_t keylen);

typedef void swarmkv_callback_func_t(const char* table, const char* key, size_t keylen,
										const char* val, size_t vallen, void *arg);
void swarmkv_get_callback(struct swarmkv_store*store, const struct swarmkv_readoptions* options, const char* key, size_t keylen, swarmkv_callback_func_t* cb, void* arg);
int swarmkv_set_callback(struct swarmkv_store* store, const char* table, const struct swarmkv_writeoptions* options, 
								const char* key, size_t keylen, const char* value, size_t vallen, swarmkv_callback_func_t* cb, void* arg);


long long swarmkv_incrby(struct swarmkv_store* store, const char* key, const struct swarmkv_writeoptions* options, size_t key_sz, long long increment);

void swarmkv_hset(struct swarmkv_store* store, const struct swarmkv_writeoptions* options, const char* key, size_t keylen, const char* field, const char* value, size_t vallen);

int swarmkv_subscribe(struct swarmkv_store* store, const char* table, cb_func callback);
struct swarmkv_reply* swarmkv_command(struct swarmkv_store*store, const char *format, ...);
value=swarm_kv_command(store, "hget %s %s", key, mbr);
void swarmkv_close(struct swarmkv_store* store);


#ifdef __cplusplus
}  /* end extern "C" */
#endif

