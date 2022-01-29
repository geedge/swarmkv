
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif


	struct swarmkv_readoptions;
	struct swarmkv_writeoptions;
	struct swarmkv_store;
	typedef void swarmkv_string_type_callback_func_t(const char * key, size_t keylen, 
		const char * value, size_t vallen, void * arg);


	// config example: "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
	// bootstraps example: "proto=udp;self=0.0.0.0:8323;peers=192.168.0.100:8323,[2001:db8::1]:8080"
	struct swarmkv_store * swarmkv_open(const char * bootstraps, const char * config, char * *err);
	void swarmkv_close(struct swarmkv_store * store);




	void swarmkv_get_string(struct swarmkv_store * store, 
		const char * key, size_t keylen, swarmkv_string_type_callback_func_t * cb, void * arg);
	void swarmkv_set_string(struct swarmkv_store * store, 
		const char * key, size_t keylen, const char * value, size_t vallen);

	char * swarmkv_del(struct swarmkv_store * store, const char * table, const struct swarmkv_readoptions * options, 
		const char * key, size_t keylen);




#ifdef __cplusplus
} /* end extern "C" */


#endif

