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
struct swarmkv_reply *swarmkv_reply_dup(const struct swarmkv_reply *origin);
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
int swarmkv_options_set_consul_host(struct swarmkv_options *opts, const char *ip_addr);
int swarmkv_options_set_dryrun(struct swarmkv_options *opts);
//Default 1: Enabled.
int swarmkv_options_set_run_for_leader_enabled(struct swarmkv_options *opts, int value);
//Default 0: Disabled. 
//Enabling batch synchronization requires all cluster nodes using same worker thread number.
int swarmkv_options_set_batch_sync_enabled(struct swarmkv_options *opts, int value);
int swarmkv_options_set_caller_thread_number(struct swarmkv_options *opts, int nr_caller_threads);
int swarmkv_options_set_worker_thread_number(struct swarmkv_options *opts, int nr_worker_threads);
int swarmkv_options_get_worker_thread_number(const struct swarmkv_options *opts);

//Default 1: Enabled.
//Enable snappy compression for network communication.
int swarmkv_options_set_network_compression_enabled(struct swarmkv_options *opts, int value);
/**
 * This function has change the behavior of swarmkv_caller_loop().
 * Record an interval and/or a number of callbacks after which the event base
 * should check for new events.  By default, the event base will run as many
 * events are as activated at the highest activated priority before checking
 * for new events.  If you configure it by setting max_interval, it will check
 * the time after each callback, and not allow more than max_interval to
 * elapse before checking for new events.  If you configure it by setting
 * max_callbacks to a value >= 0, it will run no more than max_callbacks
 * callbacks before checking for new events.
 * 
 * Same as event_config_set_max_dispatch_interval(), see https://libevent.org/doc/event_8h.html#aeb08f9ff880ff8a07eb623124f48f979
 *
 * @param opts The swarmkv options object.
 * @param max_interval An interval after which Libevent should stop running
 *     callbacks and check for more events, or NULL if there should be
 *     no such interval.
 * @param max_callbacks A number of callbacks after which Libevent should
 *     stop running callbacks and check for more events, or -1 if there
 *     should be no such limit.
 * @return 0 on success, -1 on failure.
 **/
int swarmkv_options_set_max_dispatch_interval(struct swarmkv_options *opts, const struct timeval *max_interval, int max_callbacks);

int swarmkv_options_set_cluster_announce_ip(struct swarmkv_options *opts, const char *ip_addr);
int swarmkv_options_set_cluster_announce_port(struct swarmkv_options *opts, unsigned int cluster_announce_port);
int swarmkv_options_set_health_check_announce_port(struct swarmkv_options *opts, unsigned int health_check_announce_port);

struct swarmkv;

/* The ownership of opts is transferred to swarmkv instance.*/
struct swarmkv *swarmkv_open(struct swarmkv_options *opts, const char *cluster_name, char **err);
void swarmkv_close(struct swarmkv * db);
struct swarmkv_options *swarmkv_get0_options(struct swarmkv *db);

void swarmkv_register_thread(struct swarmkv *db);

/* Do not exit the loop because we have no pending events. */
#define	SWARMKV_LOOP_NO_EXIT_ON_EMPTY 0x04
/* Do not block, see which events are ready now, run the callbacks of the highest-priority ones, then exit.*/
#define	SWARMKV_LOOP_NONBLOCK 0x02
/* Block until we have an active event, then exit once all active events have had their callbacks run. */
#define	SWARMKV_LOOP_ONCE 0x01
/**
 * Run the event loop for the specified amount of time. swarmkv_on_reply_callback_t will be called when the reply is ready.
 * By default, this loop will run the event base until either there are no more pending or active events, or until something calls swarmkv_caller_loop_break().
 * You can override this behavior with the 'flags' argument.
 * @param db swarmkv instance to loop, the caller thread must be registered by swarmkv_register_thread() before calling this function.
 * @param flags	any combination of SWARMKV_LOOP_ONCE | SWARMKV_LOOP_NONBLOCK | SWARMKV_LOOP_NO_EXIT_ON_EMPTY.
 * The flags are identicial to event_base_loop() of libevent. See https://libevent.org/doc/event_8h.html#a10b9c102337d0ece8607c9c01bc4f7c8
 * @param tv Timeout for select/poll in milliseconds, or NULL for no timeout.
 */
void swarmkv_caller_loop(struct swarmkv *db, int flags, struct timeval *tv);
void swarmkv_caller_loop_break(struct swarmkv *db);
long long swarmkv_caller_get_pending_commands(struct swarmkv *db);
//Blocking function
struct swarmkv_reply *swarmkv_command(struct swarmkv *db, const char *format, ...)__attribute__ ((format (printf, 2, 3)));
struct swarmkv_reply *swarmkv_command_on(struct swarmkv *db, const char *target, const char *format, ...)__attribute__ ((format (printf, 3, 4)));
struct swarmkv_reply *swarmkv_command_on_argv(struct swarmkv *db, const char *target, int argc, const char *argv[], size_t *argv_len);

//Non-blocking function
typedef void swarmkv_on_reply_callback_t(const struct swarmkv_reply *reply, void * arg);
void swarmkv_async_command(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *format, ...)__attribute__ ((format (printf, 4, 5)));
void swarmkv_async_command_on(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *target, const char *format, ...)__attribute__ ((format (printf, 5, 6)));
void swarmkv_async_command_on_argv(struct swarmkv *db, swarmkv_on_reply_callback_t * cb, void *cb_arg, const char *target, int argc, const char *argv[], size_t *argv_len);

void swarmkv_get(struct swarmkv * db, 
	const char * key, size_t keylen, swarmkv_on_reply_callback_t * cb, void * arg);
void swarmkv_set(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_on_reply_callback_t * cb, void * arg);

void swarmkv_del(struct swarmkv * db, const char * key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_incrby(struct swarmkv * db, const char * key, size_t keylen, long long delta, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_expire(struct swarmkv *db, const char *key, size_t keylen, int seconds, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_ttl(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_persist(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_sadd(struct swarmkv *db, const char *key, size_t keylen, const char *members[], const size_t members_len[], size_t n_members, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_srem(struct swarmkv *db, const char *key, size_t keylen, const char *members[], const size_t members_len[], size_t n_members, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_sismember(struct swarmkv *db, const char *key, size_t keylen, const char *member, size_t member_len, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_smembers(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_scard(struct swarmkv *db, const char *key, size_t keylen, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_tconsume(struct swarmkv * db, const char * key, size_t keylen, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_ftconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long weight, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_btconsume(struct swarmkv * db, const char * key, size_t keylen, const char * member, size_t member_len, long long tokens, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_bfadd(struct swarmkv * db, const char * key, size_t keylen, const char *items[], const size_t items_len[], size_t n_items, swarmkv_on_reply_callback_t *cb, void *cb_arg);
void swarmkv_bfmexists(struct swarmkv * db, const char * key, size_t keylen, const char *items[], const size_t items_len[], size_t n_items, swarmkv_on_reply_callback_t *cb, void *cb_arg);

void swarmkv_cmsincrby(struct swarmkv * db, const char * key, size_t keylen, const char * items[], const size_t items_len[], long long increments[], size_t n_increment, swarmkv_on_reply_callback_t * cb, void * cb_arg);
void swarmkv_cmsmquery(struct swarmkv * db, const char * key, size_t keylen, const char * items[], const size_t items_len[], size_t n_items, swarmkv_on_reply_callback_t * cb, void * cb_arg);

//Used by swarmkv-cli
size_t swarmkv_get_possible_command_name(struct swarmkv *db, const char *prefix, const char *cmd_names[], size_t sz);
char *swarmkv_get_command_hint(struct swarmkv *db, const char* cmd_name);


const char *swarmkv_self_address(const struct swarmkv *db);
void swarmkv_self_uuid(const struct swarmkv *db, char buff[37]);
int swarmkv_gettid(const struct swarmkv *db);
#ifdef __cplusplus
} /* end extern "C" */
#endif

