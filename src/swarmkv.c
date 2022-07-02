#include "swarmkv/swarmkv.h"

#include "swarmkv_common.h"
#include "swarmkv_rpc.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv_limits.h"
#include "swarmkv_utils.h"
#include "swarmkv_error.h"

#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"

#include "t_string.h"
#include "t_crdt.h"

#include "swarmkv_message.h"
#include "swarmkv_net.h"
#include "future_promise.h"

#include "uthash.h"
#include "sds.h"

#include <stdio.h>
#include <assert.h>
#include <uuid/uuid.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <pthread.h>


int SWARMKV_VERSION_2_0_0_year_of_tiger=0;
#define	MODULE_SWAMRKV_CORE	module_name_str("swarmkv.core")

struct swarmkv_thread_ctx
{
	pthread_t thr;
	int readable_tid;
	unsigned int thread_id;
	struct swarmkv *db;
	time_t lastime;
};


struct swarmkv
{
	char db_name[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	struct swarmkv_options *opts;
	int nr_worker_threads;
	struct swarmkv_node_addr self;
	struct swarmkv_thread_ctx *worker_threads[SWARMKV_THREAD_MAX];
	struct swarmkv_rpc_mgr *rpc_mgr;
	struct swarmkv_net *net;
	pthread_mutex_t mutex;
	void *logger;
	unsigned long long rr_sequence_generator;
	struct swarmkv_module *mod_keyspace;
	struct swarmkv_module *mod_store;
	struct swarmkv_cmd_spec *command_table;
};
void __exec_cmd(struct swarmkv *db, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *target_node, struct future *future_of_caller);

static void spec_lock_module(struct swarmkv_cmd_spec *spec)
{
	if(spec->flag==CMD_KEY_RO||spec->flag==CMD_KEY_NA)
	{
		pthread_rwlock_rdlock(&(spec->module->rwlock));
	}
	else
	{
		pthread_rwlock_wrlock(&(spec->module->rwlock));
	}

}
static void spec_unlock_module(struct swarmkv_cmd_spec *spec)
{
	pthread_rwlock_unlock(&(spec->module->rwlock));
}

struct swarmkv_cmd_spec *get_spec_by_argv(struct swarmkv *db, size_t argc, char* const argv[])
{
	struct swarmkv_cmd_spec *spec=NULL;
	char name[256]="";
	strncpy(name, argv[0], sizeof(name));
	toUpper(name);
	HASH_FIND(hh, db->command_table, name, strlen(name), spec);
	if(spec)
	{
		return spec;
	}
	if(argc<2)
	{
		return NULL;
	}
	snprintf(name, sizeof(name), "%s %s", argv[0], argv[1]);
	toUpper(name);
	HASH_FIND(hh, db->command_table, name, strlen(name), spec);
	if(spec)
	{
		return spec;
	}
	return NULL;
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

void cmd_exec_cb(const struct swarmkv_reply *reply, void * arg)
{
	struct future *f = (struct future *) arg;
	struct promise* p = future_to_promise(f);
	if(reply)
	{
		promise_success(p, (void*)reply);
	}
	else
	{
		promise_failed(p, FUTURE_ERROR_TIMEOUT, "timeout");
	}
}

void exec_command_at_peer(struct swarmkv *db, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *peer_addr, struct future* f)
{
	unsigned long long rpc_seq=swarmkv_rpc_set_callback(db->rpc_mgr, cmd_exec_cb, f);
	struct swarmkv_msg *msg=swarmkv_msg_new(db->bin_uuid, rpc_seq);

	//swarmkv_msg_add0_cmd doesn't change the cmd's ownership
	swarmkv_msg_add0_cmd(msg, cmd);
	sds buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(db->net, peer_addr, buff);
	
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;	
	return;
}

static struct swarmkv_reply *cmd_flag_to_keyspace_no_obj_owner_reply(unsigned long long cmd_flag)
{
	struct swarmkv_reply *reply=NULL;
	switch(cmd_flag)
	{
		case CMD_KEY_RO:
			reply=swarmkv_reply_new_nil();
			break;
		case CMD_KEY_OW:
			reply=swarmkv_reply_new_error(error_keyspace_obj_owner_not_found);
			break;
		case CMD_KEY_RM:
			reply=swarmkv_reply_new_integer(0);
			break;
		case CMD_KEY_RW:
			reply=swarmkv_reply_new_integer(0);
			break;
		default:
			assert(0);
			break;
	}
	return reply;
}

static struct swarmkv_cmd *make_keyroute_cmd(enum cmd_key_flag flag, const sds key, const struct swarmkv_node_addr *obj_owner)
{
	struct swarmkv_cmd *keyroute_cmd=NULL;
	switch(flag)
	{
		case CMD_KEY_RO:
		case CMD_KEY_RW:
			if(obj_owner)
			{
				keyroute_cmd=swarmkv_cmd_new(5);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("xradd");
				keyroute_cmd->argv[2]=sdsdup(key);
				node_addr2sds(obj_owner, keyroute_cmd->argv+3);
			}
			else
			{
				keyroute_cmd=swarmkv_cmd_new(3);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("rlist");
				keyroute_cmd->argv[2]=sdsdup(key);
			}
			break;
		case CMD_KEY_OW:
			if(obj_owner)
			{
				keyroute_cmd=swarmkv_cmd_new(5);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("radd");
				keyroute_cmd->argv[2]=sdsdup(key);
				node_addr2sds(obj_owner, keyroute_cmd->argv+3);
			}
			else
			{
				keyroute_cmd=swarmkv_cmd_new(3);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("radd");
				keyroute_cmd->argv[2]=sdsdup(key);
			}				
			break;
		case CMD_KEY_RM:
			assert(0);
			keyroute_cmd=swarmkv_cmd_new(3);
			keyroute_cmd->argv[0]=sdsnew("keyspace");
			keyroute_cmd->argv[1]=sdsnew("del");
			keyroute_cmd->argv[2]=sdsdup(key);
			break;
		default:
			assert(0);
			break;
	}
	return keyroute_cmd;
}
int is_sufficient_arg_num(const struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd)
{
	size_t expect_argc=0;
	if(strchr(spec->name, ' '))
	{
		expect_argc=spec->arity+2;
	}
	else
	{
		expect_argc=spec->arity+1;
	}
	if(cmd->argc<expect_argc)
	{
		return 0;
	}
	return 1;
}
struct cmd_ctx
{
	struct swarmkv *db;
	struct swarmkv_cmd_spec *spec;
	struct swarmkv_cmd *cmd;
	int redirect_cnt;
	struct future *future_of_caller;
	struct future *future_of_mine;
};

struct cmd_ctx *cmd_ctx_new(struct swarmkv *db, const struct swarmkv_cmd *cmd, struct swarmkv_cmd_spec *spec, struct future *future_of_caller)
{
	struct cmd_ctx *ctx=ALLOC(struct cmd_ctx, 1);

	ctx->db=db;
	ctx->spec=spec;
	ctx->cmd=swarmkv_cmd_dup(cmd);
	ctx->redirect_cnt=0;
	ctx->future_of_caller=future_of_caller;
/*
	char my_future_name[SWARMKV_SYMBOL_MAX];
	strncpy(my_future_name, spec->name, sizeof(my_future_name));
	toLower(my_future_name);
	replace_char(my_future_name, ' ', '_');
	ctx->future_of_mine=future_create(my_future_name, key_route_on_success, generic_on_fail, ctx);
*/
	return ctx;
}
void cmd_ctx_free(struct cmd_ctx *ctx)
{
	swarmkv_cmd_free(ctx->cmd);
	if(ctx->future_of_mine) future_destroy(ctx->future_of_mine);
	free(ctx);
	return;
}
static void peer_exec_on_success(void* result, void* user)
{	
	struct cmd_ctx* ctx = (struct cmd_ctx*)user;
	struct swarmkv_reply *reply = (struct swarmkv_reply*) result;
	struct swarmkv_node_addr peer;
	if(reply->type==SWARMKV_REPLY_NODE && 0==strncasecmp(reply->str, "-ASK", 4))
	{
		ctx->redirect_cnt++;
		swarmkv_reply2node_addr(reply, &peer);
		exec_command_at_peer(ctx->db, ctx->cmd, &peer, ctx->future_of_mine);
	}
	else
	{
		struct promise *p=future_to_promise(ctx->future_of_caller);
		promise_success(p, reply);
		cmd_ctx_free(ctx);
	}
}

static void key_route_on_success(void* result, void* user)
{
	struct cmd_ctx* ctx = (struct cmd_ctx*)user;
	struct swarmkv_reply *reply = (struct swarmkv_reply*) result;
	struct swarmkv_reply *user_reply_for_keyspace_not_found=NULL;
	size_t n_replica_node=0;
	struct swarmkv_node_addr *replica_nodes=NULL;

	swarmkv_reply2nodelist(reply, &replica_nodes, &n_replica_node);
	if(n_replica_node==0)
	{
		struct promise *p=future_to_promise(ctx->future_of_caller);
		user_reply_for_keyspace_not_found=cmd_flag_to_keyspace_no_obj_owner_reply(ctx->spec->flag);
		promise_success(p, user_reply_for_keyspace_not_found);
		swarmkv_reply_free(user_reply_for_keyspace_not_found);
		user_reply_for_keyspace_not_found=NULL;
	}
	else
	{		
		const sds key=ctx->cmd->argv[ctx->spec->key_offset];
		int self_is_a_replica=swarmkv_node_addr_list_exists(replica_nodes, n_replica_node, &ctx->db->self);
		if(self_is_a_replica)
		{
			n_replica_node-=swarmkv_node_addr_list_remove(replica_nodes, n_replica_node, &ctx->db->self);
			swarmkv_store_add_key(ctx->db->mod_store, key, replica_nodes, n_replica_node);
		}
		if(self_is_a_replica && n_replica_node==0)
		{
			__exec_cmd(ctx->db, ctx->cmd, NULL, ctx->future_of_caller);
		}
		else
		{
			exec_command_at_peer(ctx->db, ctx->cmd, replica_nodes+0, ctx->future_of_caller);
		}
		free(replica_nodes);		
	}
	ctx->future_of_caller=NULL;
	cmd_ctx_free(ctx);
}
static void generic_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cmd_ctx *ctx=(struct cmd_ctx*)user;
	struct promise *p=future_to_promise(ctx->future_of_caller);
	promise_failed(p, err, what);
	ctx->future_of_caller=NULL;
	cmd_ctx_free(ctx);
}

void __exec_cmd(struct swarmkv *db, const struct swarmkv_cmd *cmd, const struct swarmkv_node_addr *target_node, struct future *future_of_caller)
{
	struct swarmkv_cmd_spec *spec=NULL;
	struct swarmkv_reply *reply=NULL;
	enum cmd_exec_result exec_ret=FINISHED;
	struct swarmkv_node_addr peer;
	struct cmd_ctx *ctx=NULL;
	struct promise *p=NULL;
	int ret=0;
	struct swarmkv_cmd *keyspace_cmd=NULL;
	
	spec=get_spec_by_argv(db, cmd->argc, cmd->argv);
	if(!spec)
	{		
		reply=swarmkv_reply_new_error(error_unknown_command, cmd->argv[0], cmd->argc>1?cmd->argv[1]:" ");
		p=future_to_promise(future_of_caller);
		promise_success(p, reply);
		swarmkv_reply_free(reply);
		return;
	}
	if(!is_sufficient_arg_num(spec, cmd))//error happens
	{
		reply=swarmkv_reply_new_error(error_wrong_number_of_arg, spec->name);
		p=future_to_promise(future_of_caller);
		promise_success(p, reply);
		swarmkv_reply_free(reply);
		return;
	}
	ctx=cmd_ctx_new(db, cmd, spec, future_of_caller);
	if(target_node && swarmkv_node_addr_compare(&db->self, target_node))
	{
		ctx->future_of_mine=future_create("peer_exec", peer_exec_on_success, generic_on_fail, ctx);
		exec_command_at_peer(ctx->db, ctx->cmd, target_node, ctx->future_of_mine);
		return;
	}
	spec_lock_module(spec);
	exec_ret=spec->proc(spec->module, cmd, NULL, &reply);
	spec_unlock_module(spec);
	switch(exec_ret)
	{
		case FINISHED:
			p=future_to_promise(future_of_caller);
			promise_success(p, reply);
			cmd_ctx_free(ctx);
			ctx=NULL;
			break;
		case REDIRECT:
			ret=swarmkv_reply2node_addr(reply, &peer);
			assert(ret==0);
			ctx->future_of_mine=future_create("peer_exec", peer_exec_on_success, generic_on_fail, ctx);
			exec_command_at_peer(ctx->db, ctx->cmd, &peer, ctx->future_of_mine);
			break;
		case NEED_KEY_ROUTE:
			keyspace_cmd=make_keyroute_cmd(spec->flag, cmd->argv[spec->key_offset], db->opts->dryrun? NULL:(&db->self));
			ctx->future_of_mine=future_create("key_route", key_route_on_success, generic_on_fail, ctx);
			__exec_cmd(db, keyspace_cmd, NULL, ctx->future_of_mine);
			break;
		default:
			assert(0);
			break;
	}
	if(reply) swarmkv_reply_free(reply);
	return;
}
struct caller_ctx
{
	swarmkv_cmd_callback_func_t * cb;
	void * cb_arg;
	struct future *my_future;
};
static void exec_cmd_on_success(void* result, void* user)
{	
	struct caller_ctx *ctx=(struct caller_ctx*)user;
	struct swarmkv_reply *reply=(struct swarmkv_reply *)result;
	ctx->cb(reply, ctx->cb_arg);
	future_destroy(ctx->my_future);
	free(ctx);
}
static void exec_cmd_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct caller_ctx *ctx=(struct caller_ctx*)user;
	struct swarmkv_reply *reply=swarmkv_reply_new_error(what);
	ctx->cb(reply, ctx->cb_arg);
	future_destroy(ctx->my_future);
	free(ctx);
	swarmkv_reply_free(reply);
}

void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	struct caller_ctx *ctx=NULL;
	ctx=ALLOC(struct caller_ctx, 1);
	ctx->cb=cb;
	ctx->cb_arg=cb_arg;
	ctx->my_future=future_create("exec_cmd", exec_cmd_on_success, exec_cmd_on_fail, ctx);
	__exec_cmd(db, cmd, NULL, ctx->my_future);

	return;
}

void cmd_msg_processing(struct swarmkv* db, uuid_t requester_uuid, unsigned long long rr_seq, const struct swarmkv_node_addr *peer, const struct swarmkv_cmd *cmd)
{
	struct swarmkv_reply* reply=NULL;
	struct swarmkv_cmd_spec *spec=NULL;
	spec=get_spec_by_argv(db, cmd->argc, cmd->argv);
	if(!spec)
	{
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown command %d", cmd->argv[0]);
		return;
	}
	if(is_sufficient_arg_num(spec, cmd))
	{
		spec_lock_module(spec);
		spec->proc(spec->module, cmd, peer, &reply);
		spec_unlock_module(spec);
	}
	else
	{
		reply=swarmkv_reply_new_error(error_wrong_number_of_arg, spec->name);
	}


	struct swarmkv_msg* reply_msg=NULL;
	reply_msg=swarmkv_msg_new(requester_uuid, rr_seq);
	swarmkv_msg_add_reply(reply_msg, reply);
//	reply=NULL;
	sds buff=swarmkv_msg_serialize(reply_msg);
	swarmkv_net_send(db->net, peer, buff);
	swarmkv_msg_free(reply_msg);
	reply_msg=NULL;
	sdsfree(buff);
	buff=NULL;
	return;
}
void reply_msg_processing(struct swarmkv* db, uuid_t requester_uuid, unsigned long long rpc_seq, const struct swarmkv_node_addr *peer, const struct swarmkv_reply* reply)
{
	swarmkv_rpc_callback_func_t * cb_func=NULL;
	void *cb_arg=NULL;
	int ret=0;
	char str_uuid[37];
	if(uuid_compare(requester_uuid, db->opts->bin_uuid))
	{
		uuid_unparse(requester_uuid, str_uuid);
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown reply uuid=%s rpc_seq=%llu", str_uuid, rpc_seq);
		return;
	}
	ret=swarmkv_rpc_get_callback(db->rpc_mgr, rpc_seq, &cb_func, &cb_arg);
	if(!ret)
	{
		MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown rpc_seq %llu", rpc_seq);
		return;
	}
	cb_func(reply, cb_arg);
	return;
}
void msg_read_callback(const struct swarmkv_node_addr * peer, const char* data, size_t len, void* arg)
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

void command_register(struct swarmkv_cmd_spec **table, const char *name, const char *hint,
						int arity, int key_offset, enum cmd_key_flag flag,
						command_proc_func *proc, struct swarmkv_module *module)
{
	struct swarmkv_cmd_spec *spec=NULL;

	spec=ALLOC(struct swarmkv_cmd_spec, 1);
	spec->name=name;
	spec->hint=hint;
	spec->arity=arity;
	spec->key_offset=key_offset;
	spec->flag=flag;
	spec->proc=proc;

	spec->module=module;
	HASH_ADD_KEYPTR(hh, *table, spec->name, strlen(spec->name), spec);
	return;
}
void command_spec_init(struct swarmkv *db)
{
	command_register(&(db->command_table), "GET", "key",
					1, 1, CMD_KEY_RO,
					get_command, db->mod_store);
	command_register(&(db->command_table), "SET", "key value",
					2, 1, CMD_KEY_OW,
					set_command, db->mod_store);
	command_register(&(db->command_table), "INCRBY", "key increment",
					2, 1, CMD_KEY_OW,
					incrby_command, db->mod_store);
	command_register(&(db->command_table), "DEL", "key",
					1, 1, CMD_KEY_RM,
					del_command, db->mod_keyspace);
	command_register(&(db->command_table), "EXPIRE", "key seconds",
					2, 1, CMD_KEY_RW,
					expire_command, db->mod_keyspace);
	command_register(&(db->command_table), "TTL", "key",
					1, 1, CMD_KEY_RO,
					ttl_command, db->mod_keyspace);
	command_register(&(db->command_table), "PERSIST", "key",
					1, 1, CMD_KEY_RW,
					persist_command, db->mod_keyspace);

	/* low-level state-based CRDT synchronization command*/
	command_register(&(db->command_table), "CRDT PULL", "key",
					1, 2, CMD_KEY_NA,
					crdt_pull_command, db->mod_store);
	command_register(&(db->command_table), "CRDT PUSH", "key blob",
					2, 2, CMD_KEY_NA,
					crdt_push_command, db->mod_store);
	command_register(&(db->command_table), "CRDT DEL", "key",
					1, 2, CMD_KEY_NA,
					crdt_del_command, db->mod_store);

	/* low-level keyspace operation command*/
	command_register(&(db->command_table), "KEYSPACE RLIST", "key",
					1, 2, CMD_KEY_RO,
					keyspace_rlist_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE RADD", "key IP port",
					1, 2, CMD_KEY_OW,
					keyspace_radd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE XRADD", "key IP port",
					1, 2, CMD_KEY_OW,
					keyspace_xradd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE KEYS", "IP port pattern",
					3, -1, CMD_KEY_RO,
					keyspace_keys_command, db->mod_keyspace);

	/* key slot migration commands */
	command_register(&(db->command_table), "KEYSPACE SETSLOT", "node-IP node-port <slot> IMPORTING peer-IP peer-port | MIGRATING peer-IP peer-port | NODE peer-IP peer-port | STABLE",
					1, 2, CMD_KEY_NA,
					keyspace_setslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE GETKEYSINSLOT", "IP port slot",
					1, 2, CMD_KEY_NA,
					keyspace_getkeysinslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE ADDKEYSTOSLOT", "IP port slot blob",
					1, 2, CMD_KEY_NA,
					keyspace_addkeystoslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE DELSLOTKEYS", "IP port slot",
					1, 2, CMD_KEY_NA,
					keyspace_delslotkeys_command, db->mod_keyspace);

	/* cluster commands are defined in swarmkv-cli.c */
	return;
}

const struct swarmkv_cmd_spec *get_spec_by_op_str(struct swarmkv *db, const char *op_str)
{
	const struct swarmkv_cmd_spec *spec=NULL;
	char name[256]="", *p=NULL;
	size_t name_len=0;
	strncpy(name, op_str, sizeof(name));
	toLower(name);
	p=strchr(name, ' ');
	if(p)
		name_len=name-p;	
	else
		name_len=strlen(name);
	HASH_FIND(hh, db->command_table, name, name_len, spec);
	if(spec)
	{
		return spec;
	}
	p=strchr(p, ' ');
	if(p)
		name_len=name-p;
	else
		name_len=strlen(name);
	HASH_FIND(hh, db->command_table, name, name_len, spec);
	if(spec)
	{
		return spec;
	}
	return NULL;
}
size_t swarmkv_get_possible_command_name(struct swarmkv *db, const char *prefix, const char* cmd_names[], size_t sz)
{
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	size_t n_matched=0;
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		if(0==strncasecmp(spec->name, prefix, strlen(prefix)) && n_matched<sz)
		{
			cmd_names[n_matched]=spec->name;
			n_matched++;
		}
	}
	return n_matched;
}
const char *swarmkv_get_command_hint(struct swarmkv *db, const char* cmd_name)
{
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		if(0==strcasecmp(spec->name, cmd_name))
		{
			return spec->hint;
		}
	}
	return NULL;
}
struct swarmkv *swarmkv_open(struct swarmkv_options* opts, const char * db_name, char **err)
{
	struct swarmkv *db = NULL;
	future_promise_library_init(NULL);

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
	db->rpc_mgr=swarmkv_rpc_mgr_new();
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
	db->mod_keyspace=swarmkv_keyspace_new(db->opts, db_name, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_keyspace_set_exec_cmd_func(db->mod_keyspace, __exec_cmd, db);

	db->mod_store=swarmkv_store_new(__exec_cmd, db, opts->bin_uuid);
	swarmkv_net_timer_add(db->net, 0, 10, 1, (timeout_cb*)swarmkv_store_periodic, db->mod_store);

	command_spec_init(db);
	swarmkv_threads_run(db);
	char str_uuid[37]="";
	uuid_unparse(db->opts->bin_uuid, str_uuid);
	opts->is_assigned_to_db=1;
	MESA_handle_runtime_log(db->logger, RLOG_LV_INFO, MODULE_SWAMRKV_CORE, "swarmkv instance %s of cluster %s is opened.", str_uuid, db->db_name);
	return db;
error_out:
	if(db->net)
	{
		swarmkv_net_free(db->net);
		db->net=NULL;
	}
	if(db->mod_keyspace)
	{
		swarmkv_keyspace_free(db->mod_keyspace);
		db->mod_keyspace=NULL;
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
	swarmkv_store_free(db->mod_store);
	db->mod_store=NULL;
	swarmkv_keyspace_free(db->mod_keyspace);
	db->mod_keyspace=NULL;
	
	swarmkv_rpc_mgr_free(db->rpc_mgr);
	swarmkv_net_free(db->net);
	db->net=NULL;

	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		HASH_DELETE(hh, db->command_table, spec);
		free(spec);
	}

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



