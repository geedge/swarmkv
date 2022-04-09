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
	enum swarmkv_reply_type type;
	long long integer;	/* The integer when type is SWARMKV_REPLY_INTEGER */
	int len; /* Length of string */
	char *str; /* Used for both SWARMKV_REPLY_ERROR and SWARMKV_REPLY_STRING */
	size_t n_element;	 /* number of elements, for SWARMKV_REPLY_ARRAY */
	struct swarmkv_reply **elements;/* elements vector for SWARMKV_REPLY_ARRAY */
};
void swarmkv_reply_free(struct swarmkv_reply *reply);

typedef void swarmkv_cmd_callback_func_t(const struct swarmkv_reply* reply, void * arg);

struct swarmkv;
struct swarmkv_options;
struct swarmkv_options* swarmkv_options_new(void);
int swarmkv_options_set_p2p_port(struct swarmkv_options *opts, unsigned int p2p_port);
int swarmkv_options_set_health_check_port(struct swarmkv_options *opts, unsigned int health_check_port);
int swarmkv_options_set_bind_address(struct swarmkv_options *opts, const char* ip_addr);
int swarmkv_options_set_logger(struct swarmkv_options *opts, void *logger);
int swarmkv_options_set_consul_port(struct swarmkv_options *opts, unsigned int consul_port);
int swarmkv_options_set_dryrun(struct swarmkv_options *opts);
int swarmkv_options_set_worker_thread_number(struct swarmkv_options *opts, size_t nr_worker_threads);

struct swarmkv *swarmkv_open(struct swarmkv_options *opts, const char * db_name, char **err);
void swarmkv_close(struct swarmkv * db);



struct swarmkv_reply* swarmkv_command(struct swarmkv *db, const char *format, ...);

void swarmkv_get_string(struct swarmkv * db, 
	const char * key, size_t keylen, swarmkv_cmd_callback_func_t * cb, void * arg);
void swarmkv_set_string(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_cmd_callback_func_t * cb, void * arg);

void swarmkv_del(struct swarmkv * db, const char * key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg);
void swarmkv_incrby(struct swarmkv * db, const char * key, size_t keylen, long long delta, swarmkv_cmd_callback_func_t *cb, void *cb_arg);

size_t swarmkv_get_possible_command_name(struct swarmkv *db, const char *prefix, const char* cmd_names[], size_t sz);
const char *swarmkv_get_command_hint(struct swarmkv *db, const char* cmd_name);



#ifdef __cplusplus
} /* end extern "C" */


#endif

