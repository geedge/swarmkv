#include "swarmkv/swarmkv.h"

#include "swarmkv_common.h"
#include "swarmkv_limits.h"
#include "swarmkv_utils.h"

#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_message.h"
#include "swarmkv_net.h"
#include "future_promise.h"

#include "uthash.h"
#include "sds.h"

#include <MESA/MESA_handle_logger.h>

#include <sys/syscall.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <assert.h>
#include <uuid/uuid.h>
#include <time.h>


int SWARMKV_VERSION_2_0_0_tiger_year=0;
#define	MODULE_SWAMRKV_CORE	module_name_str("swarmkv.core")

struct swarmkv_thread_ctx
{
	pthread_t thr;
	int readable_tid;
	unsigned int thread_id;
	struct swarmkv *db;
	time_t lastime;
};

typedef void swarmkv_rr_callback_func_t(const struct swarmkv_reply* reply, void * arg);
//rr: request and response
struct swarmkv_rr
{
	unsigned long long sequence;
	struct timespec create_time;
	swarmkv_rr_callback_func_t* callback_func;
	void* callback_arg;
	UT_hash_handle hh;
};
struct swarmkv
{
	char db_name[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	struct swarmkv_options *opts;
	int nr_worker_threads;
	struct swarmkv_node self;
	struct swarmkv_thread_ctx *worker_threads[SWARMKV_THREAD_MAX];
	struct swarmkv_keyspace *ks;
	struct swarmkv_rr *rr_table;
	struct swarmkv_store *store;
	struct swarmkv_net *net;
	pthread_rwlock_t rwlock;
	void *logger;
	unsigned long long rr_sequence_generator;

};
struct swarmkv_readoptions
{

};
struct swarmkv_writeoptions
{

};
struct swarmkv_reply *store_exec_and_sync_cache(struct swarmkv *db, const struct swarmkv_cmd *cmd, const struct swarmkv_node *accessing_node);


static struct swarmkv_rr* swarmkv_rr_new(struct swarmkv *db, swarmkv_rr_callback_func_t * cb, void * cb_arg)
{
	struct swarmkv_rr *rr=ALLOC(struct swarmkv_rr, 1);
	rr->sequence=db->rr_sequence_generator++;
	rr->callback_func=cb;
	rr->callback_arg=cb_arg;
	HASH_ADD(hh, db->rr_table, sequence, sizeof(rr->sequence), rr);
	return rr;
}
static void swarmkv_rr_free(struct swarmkv *db, struct swarmkv_rr *rr)
{

	HASH_DELETE(hh, db->rr_table, rr);
	free(rr);
}

void swarmkv_create_thread_ctx(struct swarmkv *db)
{
	int i=0;
	for(i=0; i<db->opts->nr_worker_threads; i++)
	{
		db->worker_threads[i] = ALLOC(struct swarmkv_thread_ctx, 1);
		db->worker_threads[i]->thread_id = i;
		db->worker_threads[i]->db = db;		
	}
}
struct cmd_exec_ctx
{
	struct swarmkv_cmd *cmd;
	swarmkv_cmd_callback_func_t * cb;
	void * cb_arg;
	struct swarmkv *db;
	struct future *f_get_obj_owner;
	struct future *f_exec_cmd;
};
void exec_cmd_ctx_free(struct cmd_exec_ctx *ctx)
{
	if(ctx->f_get_obj_owner)
	{
		future_destroy(ctx->f_get_obj_owner);
		ctx->f_get_obj_owner=NULL;
	}
	if(ctx->f_exec_cmd)
	{
		future_destroy(ctx->f_exec_cmd);
		ctx->f_exec_cmd=NULL;
	}
	if(ctx->cmd)
	{
		swarmkv_cmd_free(ctx->cmd);
		ctx->cmd=NULL;
	}
	free(ctx);
}
static void cmd_exec_on_succ(void* result, void* user)
{
	struct cmd_exec_ctx *ctx=(struct cmd_exec_ctx *)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply *)result;
	ctx->cb(reply, ctx->cb_arg);
	exec_cmd_ctx_free(ctx);
}
static void cmd_exec_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cmd_exec_ctx *ctx=(struct cmd_exec_ctx *)user;
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ERROR;
	reply->str=strdup(what);
	ctx->cb(reply, ctx->cb_arg);
	swarmkv_reply_free(reply);
	exec_cmd_ctx_free(ctx);
	return;
}
void cmd_exec_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct promise* p = (struct promise*) arg;
	if(reply)
	{
		promise_success(p, (void*)reply);
	}
	else
	{
		promise_failed(p, FUTURE_ERROR_TIMEOUT, "cmd timeout");
	}
}
void cmd_exec_async(struct future *f, struct swarmkv* db, const struct swarmkv_node *peer, const struct swarmkv_cmd *cmd)
{
	struct promise* p = future_to_promise(f);	
	
	struct swarmkv_rr *rr=swarmkv_rr_new(db, cmd_exec_cb, p);
	struct swarmkv_msg *msg=swarmkv_msg_new(db->bin_uuid, rr->sequence);

	//swarmkv_msg_add0_cmd doesn't change the cmd's ownership
	swarmkv_msg_add0_cmd(msg, cmd);
	sds buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(db->net, peer, buff);
	
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;	
	return;
}
struct keyspace_route_ctx
{
	struct swarmkv_cmd *cmd;
	int redirect_cnt;
	struct swarmkv* db;
	struct future* f;

};
void keyspace_cmd_exec_ctx_free(struct keyspace_route_ctx *ctx)
{
	swarmkv_cmd_free(ctx->cmd);
	ctx->cmd=NULL;
	ctx->db=NULL;
	ctx->f=NULL;
	free(ctx);
	return;
}
static void keyspace_route_on_succ(void* result, void* user)
{
	struct cmd_exec_ctx* ctx=(struct cmd_exec_ctx*)user;
	const struct swarmkv_node* obj_owner=(const struct swarmkv_node *)result;
	struct swarmkv *db=ctx->db;
	struct swarmkv_reply *reply=NULL;
	if(swarmkv_keyspace_is_myself(db->ks, obj_owner))
	{
		reply=store_exec_and_sync_cache(db, ctx->cmd, NULL);
		cmd_exec_on_succ(reply, ctx);
		swarmkv_reply_free(reply);
	}
	else
	{
		ctx->f_exec_cmd=future_create("store_cmd", cmd_exec_on_succ, cmd_exec_on_fail, ctx);
		cmd_exec_async(ctx->f_exec_cmd, ctx->db, obj_owner, ctx->cmd);
		future_destroy(ctx->f_get_obj_owner);
		ctx->f_get_obj_owner=NULL;
	}
	return;
}
static void keyspace_route_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cmd_exec_ctx* ctx=(struct cmd_exec_ctx*)user;
	struct swarmkv_reply *reply=NULL;
	if(!strcasecmp(what, "object owner not found"))
	{
		if(is_store_del_operator(ctx->cmd->op))
		{
			reply=swarmkv_reply_new_integer(0);
		}
		else
		{
			reply=swarmkv_reply_new_nil();
		}
	}
	else
	{
		reply=swarmkv_reply_new_error(what);
	}
	ctx->cb(reply, ctx->cb_arg);
	swarmkv_reply_free(reply);
	exec_cmd_ctx_free(ctx);
}
void keyspace_route_run(struct keyspace_route_ctx* ctx, const struct swarmkv_node *key_owner);
static void keyspace_rotue_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct keyspace_route_ctx *ctx=(struct keyspace_route_ctx *)arg;
	struct promise *p=future_to_promise(ctx->f);
	struct swarmkv_node node;
	memset(&node, 0, sizeof(node));
	enum key_route_result ret=swarmkv_keyspace_reply2node(ctx->cmd->op, reply, &node);

	switch(ret)
	{
		case NOT_FOUND:	
			promise_failed(p, FUTURE_ERROR_EXCEPTION, "object owner not found");
			break;
		case FOUND:
			promise_success(p, &node);
			break;
		case ASK:
			ctx->redirect_cnt++;
			assert(ctx->redirect_cnt<2);
			keyspace_route_run(ctx, &node);
			break;
		default:
			assert(0);
			promise_failed(p, FUTURE_ERROR_EXCEPTION, "invalid reply");			
			MESA_handle_runtime_log(ctx->db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Invalid reply for keyspace command.");
			break;
	}
	if(ret!=ASK)
	{
		keyspace_cmd_exec_ctx_free(ctx);
		ctx=NULL;
	}
	return;
}
void keyspace_route_run(struct keyspace_route_ctx* ctx, const struct swarmkv_node *key_owner)
{
	struct swarmkv_rr *rr=swarmkv_rr_new(ctx->db, keyspace_rotue_cb, ctx);
	struct swarmkv_msg *msg=swarmkv_msg_new(ctx->db->bin_uuid, rr->sequence);

	swarmkv_msg_add0_cmd(msg, ctx->cmd);
	sds buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(ctx->db->net, key_owner, buff);
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;
	return;
}
void keyspace_route_async(struct future *f, struct swarmkv* db, const struct swarmkv_node *key_owner, const struct swarmkv_cmd *keyspace_cmd)
{	
	struct keyspace_route_ctx* ctx=ALLOC(struct keyspace_route_ctx, 1);
	ctx->db=db;
	ctx->redirect_cnt=0;
	ctx->cmd=swarmkv_cmd_dup(keyspace_cmd);
	ctx->f=f;
	keyspace_route_run(ctx, key_owner);
	return;
}
struct cache_sync_ctx
{
	sds key;
	struct swarmkv_node node;
	struct future *f_send_sync_cmd;
	struct swarmkv *db;
};
void send_cache_sync_msg(struct swarmkv* db, const sds key, const struct swarmkv_cmd *sync_cmd, struct swarmkv_node *accessed_node);
void cache_sync_ctx_free(struct cache_sync_ctx* ctx)
{
	future_destroy(ctx->f_send_sync_cmd);
	ctx->f_send_sync_cmd=NULL;
	sdsfree(ctx->key);
	ctx->key=NULL;
	free(ctx);
	return;
}
static void cache_sync_on_succ(void* result, void* user)
{
	struct cache_sync_ctx *ctx=(struct cache_sync_ctx *)user;
	const struct swarmkv_reply* reply=(const struct swarmkv_reply*) result;
	if(reply->type==SWARMKV_REPLY_STATUS)
	{ 
		assert(0==strncasecmp(reply->str, "OK", 2));
	}
	else if(reply->type==SWARMKV_REPLY_INTEGER)
	{
		assert(reply->integer==1);
	}
	else
	{
		assert(0);
	}
	cache_sync_ctx_free(ctx);
	return;

}
static void cache_sync_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cache_sync_ctx *ctx=(struct cache_sync_ctx *)user;
	cache_sync_ctx_free(ctx);
	return;
}

void send_cache_sync_msg(struct swarmkv* db, const sds key, const struct swarmkv_cmd *sync_cmd, struct swarmkv_node *accessed_node)
{
	struct cache_sync_ctx* ctx=ALLOC(struct cache_sync_ctx, 1);	
	ctx->db=db;
	memcpy(&ctx->node, accessed_node, sizeof(struct swarmkv_node));
	ctx->key=sdsdup(key);
	ctx->f_send_sync_cmd=future_create("cache_sync", cache_sync_on_succ, cache_sync_on_fail, ctx);
	cmd_exec_async(ctx->f_send_sync_cmd, db, &ctx->node, sync_cmd);
	return;
}
struct swarmkv_reply *store_exec_and_sync_cache(struct swarmkv *db, const struct swarmkv_cmd *cmd, const struct swarmkv_node *accessing_node)
{
	struct swarmkv_node *accessed_node_list=NULL;
	struct swarmkv_cmd *sync_cmd=NULL;
	size_t n_accessed_node=0, i=0;
	struct swarmkv_reply *reply=NULL;

	reply=swarmkv_store_exec(db->store, cmd, accessing_node, &sync_cmd, &accessed_node_list, &n_accessed_node);
	for(i=0; i<n_accessed_node; i++)
	{
		send_cache_sync_msg(db, cmd->args[0], sync_cmd, accessed_node_list+i);
	}
	if(accessed_node_list)
	{
		free(accessed_node_list);
	}
	if(sync_cmd)
	{
		swarmkv_cmd_free(sync_cmd);
		sync_cmd=NULL;
	}
	return reply;
}

static void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	int self_is_keyowner=0, self_is_objowner=0, is_cache_hit=0;
	struct swarmkv_node key_owner, obj_owner;	
	struct swarmkv_reply *reply=NULL, *keyspace_reply=NULL;
	const sds key=cmd->args[0];
	struct swarmkv_cmd *key_route_cmd=NULL;
	enum key_route_result is_found_obj_owner;
	memset(&key_owner, 0, sizeof(key_owner));
	memset(&obj_owner, 0, sizeof(obj_owner));
	self_is_keyowner=swarmkv_keyspace_get_key_owner(db->ks, key, &key_owner);
	if(is_keyspace_operator(cmd->op))
	{
		if(self_is_keyowner)
		{
			swarmkv_keyspace_exec(db->ks, cmd, &reply);
			cb(keyspace_reply, cb_arg);
			swarmkv_reply_free(reply);
		}
		else
		{
			struct cmd_exec_ctx* cmd_ctx=ALLOC(struct cmd_exec_ctx, 1);
			cmd_ctx->cmd=swarmkv_cmd_dup(cmd);
			cmd_ctx->cb=cb;
			cmd_ctx->cb_arg=cb_arg;
			cmd_ctx->db=db;
			cmd_ctx->f_exec_cmd=future_create("ks_exec", cmd_exec_on_succ, cmd_exec_on_fail, cmd_ctx);
			cmd_exec_async(cmd_ctx->f_exec_cmd, db, &obj_owner, cmd);
		}
		return;
	}
	if(db->opts->dryrun)
	{
		key_route_cmd=swarmkv_keyspace_cmd_new(cmd->op, key, &key_owner);
	}
	else
	{
		key_route_cmd=swarmkv_keyspace_cmd_new(cmd->op, key, &(db->self));
	}
	if(self_is_keyowner)
	{
		swarmkv_keyspace_exec(db->ks, key_route_cmd, &keyspace_reply);
		is_found_obj_owner=swarmkv_keyspace_reply2node(key_route_cmd->op, keyspace_reply, &obj_owner);

		swarmkv_reply_free(keyspace_reply);
		keyspace_reply=NULL;

		if(is_found_obj_owner==NOT_FOUND)
		{
			if(is_store_del_operator(cmd->op))
			{
				reply=swarmkv_reply_new_integer(0);
			}
			else
			{
				reply=swarmkv_reply_new_nil();
			}
			cb(reply, cb_arg);
			goto fast_out;
		}
		if(is_found_obj_owner==FOUND && swarmkv_keyspace_is_myself(db->ks, &obj_owner))
		{
			self_is_objowner=1;
		}
	}
	if(self_is_objowner)
	{
		reply=store_exec_and_sync_cache(db, cmd, NULL);
		cb(reply, cb_arg);
		goto fast_out;
	}
	if(is_store_read_operator(cmd->op) && !db->opts->disable_cache)
	{
		is_cache_hit=swarmkv_store_cache_read(db->store, cmd, &reply);
		if(is_cache_hit)
		{
			cb(reply, cb_arg);
			goto fast_out;
		}
	}

	if(!self_is_keyowner || !self_is_objowner)
	{
		struct cmd_exec_ctx* cmd_ctx=ALLOC(struct cmd_exec_ctx, 1);
		cmd_ctx->cmd=swarmkv_cmd_dup(cmd);
		cmd_ctx->cb=cb;
		cmd_ctx->cb_arg=cb_arg;
		cmd_ctx->db=db;
		if(!self_is_keyowner)
		{
			cmd_ctx->f_get_obj_owner=future_create("key_route", keyspace_route_on_succ, keyspace_route_on_fail, cmd_ctx);
			keyspace_route_async(cmd_ctx->f_get_obj_owner, db, &key_owner, key_route_cmd);
		}
		else
		{
			cmd_ctx->f_exec_cmd=future_create("store_exec", cmd_exec_on_succ, cmd_exec_on_fail, cmd_ctx);
			cmd_exec_async(cmd_ctx->f_exec_cmd, db, &obj_owner, cmd);
		}
	}
	
fast_out:
	if(reply)
	{
		swarmkv_reply_free(reply);
	}
	if(key_route_cmd)
	{
		swarmkv_cmd_free(key_route_cmd);
		key_route_cmd=NULL;
	}
	return;

}

void cmd_msg_processing(struct swarmkv* db, uuid_t requester_uuid, unsigned long long rr_seq, const struct swarmkv_node *peer, const struct swarmkv_cmd *cmd)
{
	struct swarmkv_reply* reply=NULL;
	if(is_keyspace_operator(cmd->op))
	{
		swarmkv_keyspace_exec(db->ks, cmd, &reply);
	}
	else if(is_cache_operator(cmd->op))
	{
		swarmkv_store_update_cache(db->store, cmd, &reply);
	}
	else
	{
		reply=store_exec_and_sync_cache(db, cmd, peer);
	}
	struct swarmkv_msg* reply_msg=NULL;
	reply_msg=swarmkv_msg_new(requester_uuid, rr_seq);
	swarmkv_msg_add_reply(reply_msg, reply);
	reply=NULL;
	sds buff=swarmkv_msg_serialize(reply_msg);
	swarmkv_net_send(db->net, peer, buff);
	swarmkv_msg_free(reply_msg);
	reply_msg=NULL;
	sdsfree(buff);
	buff=NULL;

}
void reply_msg_processing(struct swarmkv* db, uuid_t requester_uuid, unsigned long long rr_seq, const struct swarmkv_node *peer, const struct swarmkv_reply* reply)
{
	struct swarmkv_rr *rr=NULL;
	char str_uuid[37];
	if(uuid_compare(requester_uuid, db->opts->bin_uuid))
	{
		uuid_unparse(requester_uuid, str_uuid);
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown reply uuid=%s rr_seq=%llu", str_uuid, rr_seq);
		return;
	}
	HASH_FIND(hh, db->rr_table, &rr_seq, sizeof(rr_seq), rr);
	if(!rr)
	{
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown rr_seq %llu", rr_seq);
		return;
	}
	rr->callback_func(reply, rr->callback_arg);
	swarmkv_rr_free(db, rr);
	return;
}
void msg_read_callback(const struct swarmkv_node * peer, const char* data, size_t len, void* arg)
{
	struct swarmkv* db=(struct swarmkv*)arg;
	
	enum swarmkv_msg_type msg_type;
	uuid_t msg_requester_uuid;
	struct swarmkv_msg *msg_received=NULL;

	unsigned long long rr_seq=0;
	msg_received=swarmkv_msg_deserialize(data, len);
	msg_type=swarmkv_msg_get_type(msg_received);
	rr_seq=swarmkv_msg_read_rr_sequence(msg_received);
	swarmkv_msg_read_rr_requester_uuid(msg_received, msg_requester_uuid);
	if(msg_type==SWARMKV_MSG_TYPE_CMD)
	{
		struct swarmkv_cmd *cmd=swarmkv_msg_get_cmd(msg_received);
		cmd_msg_processing(db, msg_requester_uuid, rr_seq, peer, cmd);
		swarmkv_cmd_free(cmd);
		cmd=NULL;
	}
	else//SWARMKV_MSG_TYPE_REPLY
	{
		struct swarmkv_reply* reply=swarmkv_msg_get_reply(msg_received);
		reply_msg_processing(db, msg_requester_uuid, rr_seq, peer, reply);
		swarmkv_reply_free(reply);
		reply=NULL;
	}
	swarmkv_msg_free(msg_received);
}
void *swarmkv_worker_thread(void *arg)
{
	struct swarmkv_thread_ctx* ctx = (struct swarmkv_thread_ctx *) arg;
	ctx->readable_tid = syscall(SYS_gettid);
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv-%u", ctx->thread_id);
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);

	swarmkv_net_dispath(ctx->db->net, ctx->thread_id);
	return NULL;
}

void swarmkv_threads_run(struct swarmkv *db)
{
	int i = 0;
	for (i = 0; i < db->opts->nr_worker_threads; i++)
	{
		pthread_create(&db->worker_threads[i]->thr, NULL, swarmkv_worker_thread, (void *) (db->worker_threads[i]));
	}
	return;
}

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
	return opts;
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
	opts->MESA_handle_logger=logger;
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
struct swarmkv *swarmkv_open(struct swarmkv_options* opts, const char * db_name, char **err)
{
	struct swarmkv *db = NULL;
	future_promise_library_init(NULL);
	swarmkv_common_global_init();

	db=ALLOC(struct swarmkv, 1);
	strncpy(db->db_name, db_name, sizeof(db->db_name));
	db->opts=opts;
	if(opts->MESA_handle_logger)
	{
		db->logger=opts->MESA_handle_logger;
	}
	else
	{
		char log_path[1024]="";
		snprintf(log_path, sizeof(log_path), "%s.log", db_name);
		db->logger=MESA_create_runtime_log_handle(log_path, opts->loglevel);
	}
	swarmkv_create_thread_ctx(db);

	uuid_copy(db->bin_uuid, opts->bin_uuid);
	if(opts->dryrun)
	{
	}
	db->self.p2p_port=opts->p2p_port;
	strncpy(db->self.ip_addr, opts->bind_address, sizeof(db->self.ip_addr));
	
	db->rr_sequence_generator=1;
	db->net=swarmkv_net_new(opts, &db->self, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_net_set_recv_cb(db->net, msg_read_callback, db);
		
//	struct event_base *evbase=swarmkv_net_get_eventbase(db->net, 0);
	db->ks=swarmkv_keyspace_new(db->opts, db_name, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	db->store=swarmkv_store_new();
	swarmkv_threads_run(db);
	char str_uuid[37]="";
	uuid_unparse(db->opts->bin_uuid, str_uuid);
	MESA_handle_runtime_log(db->logger, RLOG_LV_INFO, MODULE_SWAMRKV_CORE, "swarmkv instance %s of cluster %s is opened.", str_uuid, db->db_name);
	return db;
error_out:
	if(db->net)
	{
		swarmkv_net_free(db->net);
		db->net=NULL;
	}
	if(db->ks)
	{
		swarmkv_keyspace_free(db->ks);
		db->ks=NULL;
	}
	free(db->opts);
	db->opts=NULL;
	free(db);
	db=NULL;
	return NULL;
}
void swarmkv_close(struct swarmkv * db)
{
	swarmkv_net_stop(db->net);
	swarmkv_store_free(db->store);
	db->store=NULL;
	swarmkv_keyspace_free(db->ks);
	db->ks=NULL;
	
	struct swarmkv_rr *txn=NULL, *tmp_txn=NULL;
	
	HASH_ITER(hh, db->rr_table, txn, tmp_txn)
	{
		swarmkv_rr_free(db, txn);
	}
	swarmkv_net_free(db->net);
	db->net=NULL;
	MESA_handle_runtime_log(db->logger, RLOG_LV_INFO, MODULE_SWAMRKV_CORE, "swarmkv cluster %s closed.", db->db_name);
	if(db->opts->MESA_handle_logger != db->logger)
	{
		MESA_destroy_runtime_log_handle(db->logger);
		db->logger=NULL;
	}
	free(db->opts);
	db->opts=NULL;
	free(db);
	return;
}

void swarmkv_set_string(struct swarmkv * db, 
	const char * key, size_t keylen, const char * value, size_t vallen, swarmkv_cmd_callback_func_t * cb, void * cb_arg)

{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(SWARMKV_OP_SET, 2);
	cmd->args[0]=sdsnewlen(key, keylen);
	cmd->args[1]=sdsnewlen(value, vallen);

	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
	
}

void swarmkv_get_string(struct swarmkv * db,
				const char *key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(SWARMKV_OP_GET, 1);
	cmd->args[0]=sdsnewlen(key, keylen);
	swarmkv_exec(db, cmd, cb, cb_arg);
	swarmkv_cmd_free(cmd);
	return;
}
int swarmkv_hset_string(struct      swarmkv_store *store,
				const char *key, size_t keylen, const char* member, size_t memberlen, const char *value, size_t vallen)
{
	return 0;
}				
void swarmkv_del(struct swarmkv * db, const char * key, size_t keylen, swarmkv_cmd_callback_func_t *cb, void *cb_arg)
{	
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_cmd_new(SWARMKV_OP_DEL, 1);	
	cmd->args[0]=sdsnewlen(key, keylen);
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
struct swarmkv_reply* swarmkv_command(struct swarmkv *db, const char *format, ...)
{
	char *cmd_str=NULL;
    va_list ap;
    va_start(ap,format);
	vasprintf(&cmd_str, format, ap);
    va_end(ap);
	char *sub_tokens[256];
	int i=0, n_subtoken=0, pos=0;
	char *token=NULL, *saveptr=NULL;
	for (token = cmd_str; n_subtoken<256; token= NULL, n_subtoken++)
	{
		sub_tokens[n_subtoken] = strtok_r(token, " ", &saveptr);
		if (sub_tokens[n_subtoken] == NULL)
			break;
	}
	struct swarmkv_cmd *cmd=NULL;
	struct swarmkv_reply *reply=NULL;
	enum swarmkv_operator op;
	int ret=0;
	ret=has_sub_operator(sub_tokens[0]);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error("unknown command %s", sub_tokens[0]);
		return reply;
	}
	if(ret)
	{
		ret=command_str2op(sub_tokens[0], sub_tokens[1], &op);
		pos+=2;
	}
	else
	{
		ret=command_str2op(sub_tokens[0], NULL, &op);
		pos++;
	}
	if(ret<0)
	{
		reply=swarmkv_reply_new_error("unknown command %s", sub_tokens[0]);
		return reply;
	}
	cmd=swarmkv_cmd_new(op, n_subtoken-pos);
	for(i=0; i<cmd->n_args; i++)
	{
		cmd->args[i]=sdsnew(sub_tokens[pos+i]);
	}
	struct blocking_query_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	pthread_cond_init(&ctx.cond, NULL);
	pthread_mutex_init(&ctx.mutex, NULL);
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
	free(cmd_str);
	return reply;
}


