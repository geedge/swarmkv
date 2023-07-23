/*
 *****************************Swarm KV******************************************
 * SwarmKV is a embedded and distributed key-CRDT store with p2p networking
 * Copyright (c) 2021-2023, Zheng Chao <contact at zhengchao dot io>
 * Copyright (c) 2021-2022, Jiang Ping
 * All rights reserved.
 * Version 2022-01-24 version 3.0.0
 ******************************************************************************
*/

#pragma once
#include <stddef.h>
#include <stdio.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C"
{
#endif


enum swarmkv_reply_type
{
	SWARMKV_REPLY_STRING=0,
	SWARMKV_REPLY_ARRAY,
	SWARMKV_REPLY_INTEGER,
	SWARMKV_REPLY_DOUBLE,
	SWARMKV_REPLY_NIL,
	SWARMKV_REPLY_STATUS,
	SWARMKV_REPLY_ERROR,
	SWARMKV_REPLY_NODE,
	SWARMKV_REPLY_VERBATIM
};
struct swarmkv_reply
{
	enum swarmkv_reply_type type;
	long long integer;	/* The integer when type is SWARMKV_REPLY_INTEGER */
	double dval;		/* The double when type is SWARMKV_REPLY_DOUBLE */
	int len; 			/* Length of string */
	char *str; 			/* Used for SWARMKV_REPLY_ERROR, SWARMKV_REPLY_STATUS, SWARMKV_NODE, SWARMKV_REPLY_STRING and SWARMKV_REPLY_VERBATIM */
	char vtype[4]; 		/* Used for SWARMKV_REPLY_VERBATIM, contains the null
						terminated 3 character content type, such as "txt". */
	size_t n_element;	/* number of elements, for SWARMKV_REPLY_ARRAY */
	struct swarmkv_reply **elements;/* elements vector for SWARMKV_REPLY_ARRAY */
};
void swarmkv_reply_free(struct swarmkv_reply *reply);
void swarmkv_reply_print(const struct swarmkv_reply *reply, FILE *stream);

struct swarmkv_options;
struct swarmkv_options *swarmkv_options_new(void);
/* Once you call swarmkv_open() with the opts, the ownership is transferred, do NOT call swarmkv_options_free().*/
void swarmkv_options_free(struct swarmkv_options *opts);

int swarmkv_options_set_cluster_port(struct swarmkv_options *opts, unsigned int cluster_port);
int swarmkv_options_set_health_check_port(struct swarmkv_options *opts, unsigned int health_check_port);
int swarmkv_options_set_cluster_timeout_us(struct swarmkv_options *opts, unsigned int timeout_us);
int swarmkv_options_set_sync_interval_us(struct swarmkv_options *opts, unsigned int interval_us);
int swarmkv_options_set_bind_address(struct swarmkv_options *opts, const char* ip_addr);

int swarmkv_options_set_logger(struct swarmkv_options *opts, void *logger);
int swarmkv_options_set_log_level(struct swarmkv_options *opts, int loglevel);
int swarmkv_options_set_log_path(struct swarmkv_options *opts, const char *logpath);
int swarmkv_options_set_consul_port(struct swarmkv_options *opts, unsigned int consul_port);
int swarmkv_options_set_consul_host(struct swarmkv_options *opts, const char* ip_addr);
int swarmkv_options_set_dryrun(struct swarmkv_options *opts);
int swarmkv_options_set_disable_run_for_leader(struct swarmkv_options *opts);
int swarmkv_options_set_caller_thread_number(struct swarmkv_options *opts, int nr_caller_threads);
int swarmkv_options_set_worker_thread_number(struct swarmkv_options *opts, int nr_worker_threads);

int swarmkv_options_set_cluster_announce_ip(struct swarmkv_options *opts, const char *ip_addr);
int swarmkv_options_set_cluster_announce_port(struct swarmkv_options *opts, unsigned int cluster_announce_port);
int swarmkv_options_set_health_check_announce_port(struct swarmkv_options *opts, unsigned int health_check_announce_port);

struct swarmkv;

/* The ownership of opts is transferred to swarmkv instance.*/
struct swarmkv *swarmkv_open(struct swarmkv_options *opts, const char *cluster_name, char **err);
void swarmkv_close(struct swarmkv * db);


void swarmkv_register_thread(struct swarmkv *db);
void swarmkv_caller_loop(struct swarmkv *db, struct timeval *tv);
void swarmkv_caller_loop_break(struct swarmkv *db);

//Blocking function
struct swarmkv_reply *swarmkv_command(struct swarmkv *db,const char *format, ...);
struct swarmkv_reply *swarmkv_command_on(struct swarmkv *db, const char *target, const char *format, ...);

//Non-blocking function
typedef void swarmkv_on_reply_callback_t(const struct swarmkv_reply *reply, void * arg);
void swarmkv_async_command_on(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *target, const char *format, ...)__attribute__ ((format (printf, 5, 6)));
void swarmkv_async_command(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *format, ...)__attribute__ ((format (printf, 4, 5)));

void swarmkv_get(struct swarmkv * db, 
	const char * key, size_t keylen, swarmkv_on_reply_callback_t * cb, void * arg);
void swarmkv_set(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_on_reply_callback_t * cb, void * arg);

void swarmkv_del(struct swarmkv * db, const char * key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_incrby(struct swarmkv * db, const char * key, size_t keylen, long long delta, swarmkv_on_reply_callback_t *cb, void *cb_arg);


void swarmkv_expire(struct swarmkv *db, const char *key, size_t keylen, int seconds, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_ttl(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_persist(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);


void swarmkv_sadd(struct swarmkv *db, const char *key, size_t keylen, const char *member[], const size_t member_len[], size_t n_member, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_srem(struct swarmkv *db, const char *key, size_t keylen, const char *member[], const size_t member_len[], size_t n_member, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_sismember(struct swarmkv *db, const char *key, size_t keylen, const char *member, size_t member_len, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_smembers(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_scard(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_tconsume(struct swarmkv * db, const char * key, size_t keylen, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_ftconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long weight, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_btconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);
//Used by swarmkv-cli
size_t swarmkv_get_possible_command_name(struct swarmkv *db, const char *prefix, const char *cmd_names[], size_t sz);
char *swarmkv_get_command_hint(struct swarmkv *db, const char* cmd_name);



#ifdef __cplusplus
} /* end extern "C" */
#endif

