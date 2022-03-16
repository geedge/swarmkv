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

struct swarmkv_params
{
	unsigned int http_port;
	unsigned int p2p_port;
	int minimum_replica_num;
	int dry_run;
	int disable_cache;
};
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
	char cluster_name[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	struct swarmkv_params para;
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


static struct swarmkv_rr* swarmkv_rr_new(struct swarmkv *db, swarmkv_rr_callback_func_t * cb, void * cb_arg)
{
	struct swarmkv_rr *txn=ALLOC(struct swarmkv_rr, 1);
	txn->sequence=db->rr_sequence_generator++;
	txn->callback_func=cb;
	txn->callback_arg=cb_arg;
	HASH_ADD(hh, db->rr_table, sequence, sizeof(txn->sequence), txn);
	return txn;
}
static void swarmkv_rr_free(struct swarmkv *db, struct swarmkv_rr *txn)
{

	HASH_DELETE(hh, db->rr_table, txn);
	free(txn);
}

void swarmkv_create_thread_ctx(struct swarmkv *db)
{
	int i=0;
	for(i=0; i<db->nr_worker_threads; i++)
	{
		db->worker_threads[i] = ALLOC(struct swarmkv_thread_ctx, 1);
		db->worker_threads[i]->thread_id = i;
		db->worker_threads[i]->db = db;		
	}
}
struct store_cmd_exec_ctx
{
	struct swarmkv_cmd *cmd;
	swarmkv_cmd_callback_func_t * cb;
	void * cb_arg;
	struct swarmkv *db;
	struct future *f_get_obj_owner;
	struct future *f_exec_cmd;
};
void exec_cmd_ctx_free(struct store_cmd_exec_ctx *ctx)
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
static void store_cmd_exec_on_succ(void* result, void* user)
{
	struct store_cmd_exec_ctx *ctx=(struct store_cmd_exec_ctx *)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply *)result;
	ctx->cb(reply, ctx->cb_arg);
	exec_cmd_ctx_free(ctx);
}
static void store_cmd_exec_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct store_cmd_exec_ctx *ctx=(struct store_cmd_exec_ctx *)user;
	struct swarmkv_reply *reply=ALLOC(struct swarmkv_reply, 1);
	reply->type=SWARMKV_REPLY_ERROR;
	reply->str=strdup(what);
	ctx->cb(reply, ctx->cb_arg);
	swarmkv_reply_free(reply);
	exec_cmd_ctx_free(ctx);
	return;
}
void store_cmd_exec_cb(const struct swarmkv_reply *reply, void * arg)
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
void store_cmd_exec_async(struct future *f, struct swarmkv* db, const struct swarmkv_node *peer, const struct swarmkv_cmd *cmd)
{
	struct promise* p = future_to_promise(f);	
	
	struct swarmkv_rr *rr=swarmkv_rr_new(db, store_cmd_exec_cb, p);
	struct swarmkv_msg *msg=swarmkv_msg_new(db->bin_uuid, rr->sequence);

	//swarmkv_msg_add0_cmd doesn't get the cmd's ownership
	swarmkv_msg_add0_cmd(msg, cmd);
	sds buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(db->net, peer, buff);
	
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;	
	return;
}
struct keyspace_cmd_exec_ctx
{
	struct swarmkv_cmd *cmd;
	int redirect_cnt;
	struct swarmkv* db;
	struct future* f;

};
void keyspace_cmd_exec_ctx_free(struct keyspace_cmd_exec_ctx *ctx)
{
	swarmkv_cmd_free(ctx->cmd);
	ctx->cmd=NULL;
	ctx->db=NULL;
	ctx->f=NULL;
	free(ctx);
	return;
}
static void keyspace_exec_on_succ(void* result, void* user)
{
	struct store_cmd_exec_ctx* ctx=(struct store_cmd_exec_ctx*)user;
	const struct swarmkv_node* obj_owner=(const struct swarmkv_node *)result;

	ctx->f_exec_cmd=future_create("store_cmd", store_cmd_exec_on_succ, store_cmd_exec_on_fail, ctx);
	store_cmd_exec_async(ctx->f_exec_cmd, ctx->db, obj_owner, ctx->cmd);
	future_destroy(ctx->f_get_obj_owner);
	ctx->f_get_obj_owner=NULL;
	return;
}
static void keyspace_exec_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct store_cmd_exec_ctx* ctx=(struct store_cmd_exec_ctx*)user;
	struct swarmkv_reply *reply=NULL;
	if(!strcasecmp(what, "object owner not found"))
	{
		reply=swarmkv_reply_new_nil();
	}
	else
	{
		reply=swarmkv_reply_new_error(what);
	}
	ctx->cb(reply, ctx->cb_arg);
	swarmkv_reply_free(reply);
	exec_cmd_ctx_free(ctx);
}
void keyspace_cmd_exec_ctx_run(struct keyspace_cmd_exec_ctx* ctx, const struct swarmkv_node *key_owner);
static void keyspace_cmd_exec_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct keyspace_cmd_exec_ctx *ctx=(struct keyspace_cmd_exec_ctx *)arg;
	struct promise *p=future_to_promise(ctx->f);
	struct swarmkv_node node;

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
			keyspace_cmd_exec_ctx_run(ctx, &node);
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
void keyspace_cmd_exec_ctx_run(struct keyspace_cmd_exec_ctx* ctx, const struct swarmkv_node *key_owner)
{
	struct swarmkv_rr *rr=swarmkv_rr_new(ctx->db, keyspace_cmd_exec_cb, ctx);
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
void keyspace_cmd_exec_async(struct future *f, struct swarmkv* db, const struct swarmkv_node *key_owner, const struct swarmkv_cmd *keyspace_cmd)
{	
	struct keyspace_cmd_exec_ctx* ctx=ALLOC(struct keyspace_cmd_exec_ctx, 1);
	ctx->db=db;
	ctx->redirect_cnt=0;
	ctx->cmd=swarmkv_cmd_dup(keyspace_cmd);
	ctx->f=f;
	keyspace_cmd_exec_ctx_run(ctx, key_owner);
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
	const struct swarmkv_reply* reply=(const struct swarmkv_reply*) result;
	struct cache_sync_ctx *ctx=(struct cache_sync_ctx *)user;

	struct swarmkv_cmd *full_sync_cmd=NULL;
	struct swarmkv *db=ctx->db;

	swarmkv_store_process_sync_reply(db->store, ctx->key, &ctx->node, reply, &full_sync_cmd);
	if(full_sync_cmd)
	{
		send_cache_sync_msg(db, ctx->key, full_sync_cmd, &ctx->node);
		swarmkv_cmd_free(full_sync_cmd);
		full_sync_cmd=NULL;
	}
	cache_sync_ctx_free(ctx);
	return;

}
static void cache_sync_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cache_sync_ctx *ctx=(struct cache_sync_ctx *)user;
	cache_sync_ctx_free(ctx);
}

void send_cache_sync_msg(struct swarmkv* db, const sds key, const struct swarmkv_cmd *sync_cmd, struct swarmkv_node *accessed_node)
{
	struct cache_sync_ctx* ctx=ALLOC(struct cache_sync_ctx, 1);	
	ctx->db=db;
	memcpy(&ctx->node, accessed_node, sizeof(struct swarmkv_node));
	ctx->key=sdsdup(key);		
	ctx->f_send_sync_cmd=future_create("cache_sync", cache_sync_on_succ, cache_sync_on_fail, ctx);
	store_cmd_exec_async(ctx->f_send_sync_cmd, db, &ctx->node, sync_cmd);
	return;
}

static void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	int self_is_keyowner=0, self_is_objowner=0, is_cache_hit=0;
	struct swarmkv_node key_owner, obj_owner;	
	struct swarmkv_reply *reply=NULL, *keyspace_reply=NULL;
	const sds key=cmd->args[0];
	struct swarmkv_node *accessed_node_list=NULL;
	struct swarmkv_cmd *sync_cmd=NULL, *keyspace_cmd=NULL;
	size_t n_accessed_node=0, i=0;
	enum key_route_result is_found_obj_owner;
	keyspace_cmd=swarmkv_keyspace_cmd_new(cmd->op, key, &(db->self));
	self_is_keyowner=swarmkv_keyspace_get_key_owner(db->ks, key, &key_owner);
	if(self_is_keyowner)
	{
		swarmkv_keyspace_exec(db->ks, keyspace_cmd, &keyspace_reply);
		is_found_obj_owner=swarmkv_keyspace_reply2node(keyspace_cmd->op, keyspace_reply, &obj_owner);

		swarmkv_reply_free(keyspace_reply);
		keyspace_reply=NULL;

		if(is_found_obj_owner==FOUND && swarmkv_keyspace_is_myself(db->ks, &obj_owner))
		{
			self_is_objowner=1;
		}
	}
	if(self_is_objowner)
	{
		swarmkv_store_exec(db->store, cmd, &reply, NULL, &sync_cmd, &accessed_node_list, &n_accessed_node);
		cb(reply, cb_arg);
		goto fast_out;
	}
	if(is_read_operator(cmd->op) && !db->para.disable_cache)
	{
		is_cache_hit=swarmkv_store_cache_exec(db->store, cmd, &reply);
		if(is_cache_hit)
		{
			cb(reply, cb_arg);
			goto fast_out;
		}
	}

	if(!self_is_keyowner || !self_is_objowner)
	{
		struct store_cmd_exec_ctx* exec_cmd_ctx=ALLOC(struct store_cmd_exec_ctx, 1);
		exec_cmd_ctx->cmd=swarmkv_cmd_dup(cmd);
		exec_cmd_ctx->cb=cb;
		exec_cmd_ctx->cb_arg=cb_arg;
		exec_cmd_ctx->db=db;
		if(!self_is_keyowner)
		{
			exec_cmd_ctx->f_get_obj_owner=future_create("keysp_exec", keyspace_exec_on_succ, keyspace_exec_on_fail, exec_cmd_ctx);
			keyspace_cmd_exec_async(exec_cmd_ctx->f_get_obj_owner, db, &key_owner, keyspace_cmd);
		}
		else
		{
			exec_cmd_ctx->f_exec_cmd=future_create("store_exec", store_cmd_exec_on_succ, store_cmd_exec_on_fail, exec_cmd_ctx);
			store_cmd_exec_async(exec_cmd_ctx->f_exec_cmd, db, &obj_owner, cmd);
		}
	}
	
fast_out:
	for(i=0; i<n_accessed_node; i++)
	{
		send_cache_sync_msg(db, key, cmd, accessed_node_list+i);
	}
	
	if(accessed_node_list)
	{
		free(accessed_node_list);
	}
	if(reply)
	{
		swarmkv_reply_free(reply);
	}
	if(keyspace_cmd)
	{
		swarmkv_cmd_free(keyspace_cmd);
		keyspace_cmd=NULL;
	}
	return;

}

void cmd_msg_processing(struct swarmkv* db, uuid_t msg_initiator_uuid, unsigned long long txn_seq, const struct swarmkv_node *peer, const struct swarmkv_cmd *cmd)
{
	struct swarmkv_reply* reply=NULL;
	struct swarmkv_cmd *sync_cmd=NULL;
	struct swarmkv_node* accessed_node_list=NULL;
	size_t n_accessed_node=0;
	switch(cmd->op)
	{
		case SWARMKV_OP_KEYSPACE_SET:
		case SWARMKV_OP_KEYSPACE_SETNXGET:
		case SWARMKV_OP_KEYSPACE_GET:
		case SWARMKV_OP_KEYSPACE_DEL:
		case SWARMKV_OP_KEYSPACE_GETSLOTKEYS:
			swarmkv_keyspace_exec(db->ks, cmd, &reply);
			break;
		case SWARMKV_OP_CLUSTER_SYNC:
			swarmkv_store_cache_sync(db->store, cmd, &reply);
			break;
		default:
			swarmkv_store_exec(db->store, cmd, &reply, peer, &sync_cmd, &accessed_node_list, &n_accessed_node);
			break;
	}
	struct swarmkv_msg* reply_msg;
	reply_msg=swarmkv_msg_new(msg_initiator_uuid, txn_seq);
	swarmkv_msg_add_reply(reply_msg, reply);
	reply=NULL;
	sds buff=swarmkv_msg_serialize(reply_msg);
	swarmkv_net_send(db->net, peer, buff);
	swarmkv_msg_free(reply_msg);
	reply_msg=NULL;
	sdsfree(buff);
	buff=NULL;

	size_t i=0;
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
}
void reply_msg_processing(struct swarmkv* db, uuid_t msg_initiator_uuid, unsigned long long txn_seq, const struct swarmkv_node *peer, const struct swarmkv_reply* reply)
{
	struct swarmkv_rr *txn=NULL;	

	HASH_FIND(hh, db->rr_table, &txn_seq, sizeof(txn_seq), txn);
	if(!txn)
	{
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown transaction sequence %llu", txn_seq);
		return;
	}
	txn->callback_func(reply, txn->callback_arg);
	swarmkv_rr_free(db, txn);

/*	
	if(txn->state==STATE_WAIT_KEY_OWNER)
	{
		if(reply->type==SWARMKV_REPLY_NIL)
		{
			txn->callback_func(reply, txn->callback_arg);
			HASH_DELETE(hh, db->transaction_table, txn);
			swarmkv_cmd_free(txn->cmd);
			txn->cmd=NULL;
			free(txn);
		}
		else
		{
			if(reply->type!=SWARMKV_REPLY_ARRAY || reply->n_element!=2
				|| reply->elements[0]->type!=SWARMKV_REPLY_STRING
				|| reply->elements[1]->type!=SWARMKV_REPLY_INTEGER)
			{
				MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Invalid reply for CLUSTER OBJOWNER");
			}
			memset(&obj_owner, 0, sizeof(obj_owner));
			strncpy(obj_owner.ip_addr, reply->elements[0]->str, sizeof(obj_owner.ip_addr));
			obj_owner.p2p_port=reply->elements[1]->integer;
			data_cmd_msg=swarmkv_msg_new(db->bin_uuid, txn->sequence);
			swarmkv_msg_add0_cmd(data_cmd_msg, txn->cmd);
			txn->cmd=NULL;
			txn->state=STATE_WAIT_OBJ_ONWER;
			sds buff=NULL;
			buff=swarmkv_msg_serialize(data_cmd_msg);
			txn->cmd=swarmkv_msg_get_cmd(data_cmd_msg);
			swarmkv_net_send(db->net, &obj_owner, buff);
			swarmkv_msg_free(data_cmd_msg);
			data_cmd_msg=NULL;
			sdsfree(buff);
			buff=NULL;
		}
	}
	else
	{
		txn->callback_func(reply, txn->callback_arg);
		swarmkv_rr_free(db, txn);
	}
*/
	return;
}
void msg_read_callback(const struct swarmkv_node * peer, const char* data, size_t len, void* arg)
{
	struct swarmkv* db=(struct swarmkv*)arg;
	
	enum swarmkv_msg_type msg_type;
	uuid_t msg_initiator_uuid;
	struct swarmkv_msg *msg_received=NULL;

	unsigned long long txn_seq=0;
	msg_received=swarmkv_msg_deserialize(data, len);
	msg_type=swarmkv_msg_get_type(msg_received);
	txn_seq=swarmkv_msg_read_transaction_sequence(msg_received);
	swarmkv_msg_read_txn_initiator_uuid(msg_received, msg_initiator_uuid);
	if(msg_type==SWARMKV_MSG_TYPE_CMD)
	{
		struct swarmkv_cmd *cmd=swarmkv_msg_get_cmd(msg_received);
		cmd_msg_processing(db, msg_initiator_uuid, txn_seq, peer, cmd);
		swarmkv_cmd_free(cmd);
		cmd=NULL;
	}
	else//SWARMKV_MSG_TYPE_REPLY
	{
		struct swarmkv_reply* reply=swarmkv_msg_get_reply(msg_received);
		reply_msg_processing(db, msg_initiator_uuid, txn_seq, peer, reply);
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
	for (i = 0; i < db->nr_worker_threads; i++)
	{
		pthread_create(&db->worker_threads[i]->thr, NULL, swarmkv_worker_thread, (void *) (db->worker_threads[i]));
	}
	return;
}

struct swarmkv *swarmkv_open(const char * cluster_name, const char* self_ip, unsigned int http_port, unsigned int p2p_port, void * logger)
{
	struct swarmkv *db = NULL;
	int ret = 0;
	future_promise_library_init(NULL);
	swarmkv_common_global_init();

	db=ALLOC(struct swarmkv, 1);
	strncpy(db->cluster_name, cluster_name, sizeof(db->cluster_name));
	db->logger=logger;
	db->nr_worker_threads=2;
	db->para.http_port=http_port;
	db->para.p2p_port=p2p_port;
	swarmkv_create_thread_ctx(db);

	uuid_generate(db->bin_uuid);
	db->self.p2p_port=p2p_port;
	strncpy(db->self.ip_addr, self_ip, sizeof(db->self.ip_addr));
	
	db->rr_sequence_generator=1;
	db->net=swarmkv_net_new(&db->self, db->nr_worker_threads, db->logger);
	swarmkv_net_set_recv_cb(db->net, msg_read_callback, db);
		
	struct event_base *evbase=swarmkv_net_get_eventbase(db->net, 0);
	db->ks=swarmkv_keyspace_new(cluster_name, db->bin_uuid, evbase, http_port, &db->self, logger);
	
	if(db->para.dry_run)
	{
		swarmkv_keyspace_set_dryrun(db->ks);
	}
	ret=swarmkv_keyspace_init(db->ks);
	if(ret<0)
	{
		goto error_out;
	}
	db->store=swarmkv_store_new();
	swarmkv_threads_run(db);
	MESA_handle_runtime_log(db->logger, RLOG_LV_INFO, MODULE_SWAMRKV_CORE, "swarmkv cluster %s opened.", db->cluster_name);
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
	MESA_handle_runtime_log(db->logger, RLOG_LV_INFO, MODULE_SWAMRKV_CORE, "swarmkv cluster %s closed.", db->cluster_name);
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


