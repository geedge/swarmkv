/*
*****************************Swarm KV******************************
*	SwarmKV is a embedded and distributed key-value storage library
*	with peer-to-peer networking.
*	Authors:	Zheng Chao
*				Jiang Ping
*	Version 2022-01-24 version 2.0.0
*******************************************************************
*/

#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif


enum swarmkv_reply_type
{
	SWARMKV_REPLY_STRING=0,
	SWARMKV_REPLY_ARRAY,
	SWARMKV_REPLY_INTEGER,
	SWARMKV_REPLY_NIL,
	SWARMKV_REPLY_STATUS,
	SWARMKV_REPLY_ERROR
};
struct swarmkv_reply
{
	swarmkv_reply_type type,
	long long integer;	/* The integer when type is SWARMKV_REPLY_INTEGER */
	int len; /* Length of string */
	char *str; /* Used for both SWARMKV_REPLY_ERROR and SWARMKV_REPLY_STRING */
	size_t n_element;	 /* number of elements, for SWARMKV_REPLY_ARRAY */
	struct swarmkv_reply **element;/* elements vector for SWARMKV_REPLY_ARRAY */
};
void swarmkv_reply_free(struct swarmkv_reply *reply);

struct swarmkv;
typedef void swarmkv_cmd_callback_func_t(const swarmkv_reply* reply, void * arg);


struct swarmkv_store *swarmkv_open(const char * cluster_name, unsigned int http_port, unsigned int p2p_port, void * mesa_handle_logger);
void swarmkv_close(struct swarmkv_store * store);




void swarmkv_get_string(struct swarmkv * db, 
	const char * key, size_t keylen, swarmkv_cmd_callback_func_t * cb, void * arg);
void swarmkv_set_string(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_cmd_callback_func_t * cb, void * arg);

char * swarmkv_del(struct swarmkv_store * store, const char * table, const struct swarmkv_readoptions * options, 
	const char * key, size_t keylen);




#ifdef __cplusplus
} /* end extern "C" */


#endif

