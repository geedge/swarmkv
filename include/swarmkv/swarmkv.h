
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


	struct swarmkv_store *swarmkv_open(const char * cluster_name, unsigned int http_port, unsigned int p2p_port, void * mesa_handle_logger);
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

