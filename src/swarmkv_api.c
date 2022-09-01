#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

#include "swarmkv_utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#define	MODULE_SWAMRKV_API	module_name_str("swarmkv.api")

extern void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg);

struct swarmkv_readoptions
{

};
struct swarmkv_writeoptions
{

};

struct swarmkv_options* swarmkv_options_new(void)
{
	struct swarmkv_options *opts=ALLOC(struct swarmkv_options, 1);
	opts->consul_port=8500;
	opts->p2p_port=5210;
	opts->health_check_port=5211;
	opts->loglevel=0;
	strcpy(opts->bind_address, "127.0.0.1");
	uuid_generate(opts->bin_uuid);
	opts->nr_worker_threads=1;
	opts->is_assigned_to_db=0;
	return opts;
}
void swarmkv_options_free(struct swarmkv_options *opt)
{
	if(opt->is_assigned_to_db)
	{
		assert(0);
	}
	else
	{
		free(opt);
	}
	return;
}
int swarmkv_options_set_p2p_port(struct swarmkv_options *opts, unsigned int p2p_port)
{
	opts->p2p_port=p2p_port;
	return 0;
}
int swarmkv_options_set_health_check_port(struct swarmkv_options *opts, unsigned int health_check_port)
{
	opts->health_check_port=health_check_port;
	return 0;
}

int swarmkv_options_set_bind_address(struct swarmkv_options *opts, const char* ip_addr)
{
	strncpy(opts->bind_address, ip_addr, sizeof(opts->bind_address));
	return 0;
}
int swarmkv_options_set_logger(struct swarmkv_options *opts, void *logger)
{
	opts->logger=logger;
	return 0;
}

int swarmkv_options_set_consul_port(struct swarmkv_options *opts, unsigned int consul_port)
{
	opts->consul_port=consul_port;
	return 0;
}
int swarmkv_options_set_dryrun(struct swarmkv_options *opts)
{
	opts->dryrun=1;
	opts->p2p_port=15210;//overrides default p2p port
	return 0;
}
int swarmkv_options_set_worker_thread_number(struct swarmkv_options *opts, size_t nr_worker_threads)
{
	opts->nr_worker_threads=nr_worker_threads;
	return 0;
}


void swarmkv_set(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_cmd_callback_func_t * cb, void * cb_arg)

{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(3);
	cmd->argv[0]=sdsnew("set");
	cmd->argv[1]=sdsnewlen(key, keylen);
	cmd->argv[2]=sdsnewlen(value, vallen);

	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
	
}

void swarmkv_get(struct swarmkv * db,
				const char *key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);
	cmd->argv[0]=sdsnew("get");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}
void swarmkv_expire(struct swarmkv *db, const char *key, size_t keylen, int seconds, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(3);
	cmd->argv[0]=sdsnew("expire");
	cmd->argv[1]=sdsnewlen(key, keylen);
	cmd->argv[2]=sdsfromlonglong(seconds);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}
void swarmkv_ttl(struct swarmkv *db, const char *key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);
	cmd->argv[0]=sdsnew("ttl");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}

void swarmkv_persist(struct swarmkv *db, const char *key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);
	cmd->argv[0]=sdsnew("persist");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}
void swarmkv_sadd(struct swarmkv *db, const char* key, size_t keylen, const char *member[], const size_t member_len[], size_t n_member, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2+n_member);	
	cmd->argv[0]=sdsnew("sadd");
	cmd->argv[1]=sdsnewlen(key, keylen);
	for(size_t i=0; i<n_member; i++)
	{
		cmd->argv[2+i]=sdsnewlen(member[i], member_len[i]);
	}
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);

}
void swarmkv_srem(struct swarmkv *db, const char* key, size_t keylen, const char *member[], const size_t member_len[], size_t n_member, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2+n_member);	
	cmd->argv[0]=sdsnew("srem");
	cmd->argv[1]=sdsnewlen(key, keylen);
	for(size_t i=0; i<n_member; i++)
	{
		cmd->argv[2+i]=sdsnewlen(member[i], member_len[i]);
	}
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);

}
void swarmkv_sismember(struct swarmkv *db, const char* key, size_t keylen, const char *member, size_t member_len, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(3);	
	cmd->argv[0]=sdsnew("sismember");
	cmd->argv[1]=sdsnewlen(key, keylen);
	cmd->argv[2]=sdsnewlen(member, member_len);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
}
void swarmkv_smembers(struct swarmkv *db, const char* key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);	
	cmd->argv[0]=sdsnew("smembers");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
}
void swarmkv_scard(struct swarmkv *db, const char* key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);	
	cmd->argv[0]=sdsnew("scard");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
}

int swarmkv_hset_string(struct swarmkv *db,
				const char *key, size_t keylen, const char* member, size_t memberlen, const char *value, size_t vallen)
{
	return 0;
}				
void swarmkv_del(struct swarmkv * db, const char * key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{	
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(2);	
	cmd->argv[0]=sdsnew("del");
	cmd->argv[1]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}
void swarmkv_incrby(struct swarmkv * db, const char * key, size_t keylen, long long increment, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{	
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(3);	
	cmd->argv[0]=sdsnew("incrby");
	cmd->argv[1]=sdsnewlen(key, keylen);
	cmd->argv[2]=sdsfromlonglong(increment);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}

struct blocking_query_ctx
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	struct swarmkv_reply* reply;
};
void blocking_query_cb(const struct swarmkv_reply* reply, void * arg)
{
	struct blocking_query_ctx *ctx=(struct blocking_query_ctx*) arg;
	ctx->reply=swarmkv_reply_dup(reply);
	pthread_cond_signal(&ctx->cond);
	return;
}
struct swarmkv_reply *swarmkv_cmd_argv(struct swarmkv *db, int argc, sds *argv)
{
	struct swarmkv_cmd *cmd=NULL;
	struct swarmkv_reply *reply=NULL;
	struct blocking_query_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	pthread_cond_init(&ctx.cond, NULL);
	pthread_mutex_init(&ctx.mutex, NULL);
	cmd=swarmkv_cmd_new(argc);
	for(int i=0; i<argc; i++)
	{
		cmd->argv[i]=sdsdup(argv[i]);
	}
	swarmkv_exec(db, cmd, blocking_query_cb, &ctx);
	struct timespec max_wait = {0, 0};
	clock_gettime(CLOCK_REALTIME, &max_wait);
	max_wait.tv_sec+=1800;
	int timed_wait_rv=0;
	if(NULL==ctx.reply)
	{
		pthread_mutex_lock(&ctx.mutex);
		timed_wait_rv=pthread_cond_timedwait(&ctx.cond, &ctx.mutex, &max_wait);
		pthread_mutex_unlock(&ctx.mutex);
		if(timed_wait_rv)
		{
			printf("%s\n", swarmkv_util_pthread_cond_timedwait_error_to_string(timed_wait_rv));
			assert(0);
			ctx.reply=swarmkv_reply_new_error("timeout");
		}
	}
	reply=ctx.reply;
	ctx.reply=NULL;
	swarmkv_cmd_free(cmd);
	return reply;
}
struct swarmkv_reply *swarmkv_command(struct swarmkv *db, const char *format, ...)
{
	char *cmd_str=NULL;
    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);

	int argc=0;
	sds *argv=NULL;
	argv=sdssplitargs(cmd_str, &argc);
	struct swarmkv_reply *reply=NULL;
	reply=swarmkv_cmd_argv(db, argc, argv);

    if (argv)
        sdsfreesplitres(argv, argc);
	free(cmd_str);

	return reply;
}
