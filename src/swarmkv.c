#include "swarmkv/swarmkv.h"

#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv_limits.h"
#include "swarmkv_utils.h"
#include "swarmkv_error.h"

//header of modules
#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_monitor.h"

//header of data types
#include "t_string.h"
#include "t_set.h"
#include "t_token_bucket.h"
#include "t_crdt.h"

#include "swarmkv_message.h"
#include "swarmkv_net.h"
#include "future_promise.h"

#include "uthash.h"
#include "sds.h"
#include "log.h"

#include <stdio.h>
#include <uuid/uuid.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <pthread.h>


const char* SWARMKV_VERSION="3.0.0";
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
	struct swarmkv_module module;
	char db_name[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	struct swarmkv_options *opts;
	int nr_worker_threads;

	node_t self;
	struct swarmkv_thread_ctx *worker_threads[SWARMKV_THREAD_MAX];
	struct swarmkv_net *net;
	pthread_mutex_t mutex;
	struct log_handle * logger;
	struct swarmkv_module *mod_keyspace;
	struct swarmkv_module *mod_store;
	struct swarmkv_module *mod_monitor;
	struct swarmkv_cmd_spec *command_table;
	struct timespec boot_time;
};
struct swarmkv *module2db(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "db"));
	struct swarmkv *db=container_of(module, struct swarmkv, module);
	assert(db==module->mod_ctx);
	return db;
}
void __exec_cmd(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *target_node, struct future *future_of_caller);

static void spec_lock_module(struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd)
{
	if(!spec->module->lock) return;
	if(spec->key_offset<0)
	{
		spec->module->lock(spec->module, NULL);
	}
	else
	{
		spec->module->lock(spec->module, cmd->argv[spec->key_offset]);
	}
}
static void spec_unlock_module(struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd)
{
	if(!spec->module->unlock) return;
	if(spec->key_offset<0)
	{
		spec->module->unlock(spec->module, NULL);
	}
	else
	{
		spec->module->unlock(spec->module, cmd->argv[spec->key_offset]);
	}
}
enum cmd_exec_result info_command(struct swarmkv_module *mod_db, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
	char node_info_buff[4096], store_info_buff[4096], ks_info_buff[4096], net_info_buff[4096];
	char all_info_buff[4096*4];
	struct swarmkv *db=module2db(mod_db);
	struct timespec now_monotonic;
	long long server_time_us;
	server_time_us=ustime();
	clock_gettime(CLOCK_MONOTONIC, &now_monotonic);
	char uuid_str[37];
	uuid_unparse(db->opts->bin_uuid, uuid_str);
	snprintf(node_info_buff, sizeof(node_info_buff), "# Node\n"
											"swarmkv_version: %s\n"
											"address: %s\n"
											"uuid: %s\n"
											"worker_threads: %zu\n"
											"server_time_usec: %lld\n"
											"up_time_in_seconds: %ld\n"
											"up_time_in_days: %ld\n"
											,
											SWARMKV_VERSION,
											db->self.addr,
											uuid_str,
											db->opts->nr_worker_threads,
											server_time_us,
											now_monotonic.tv_sec-db->boot_time.tv_sec,
											(now_monotonic.tv_sec-db->boot_time.tv_sec)/(3600*24)
											);
	struct store_info sto_info;
	swarmkv_store_info(db->mod_store, &sto_info);
	snprintf(store_info_buff, sizeof(store_info_buff), "# Store\n"
													"shards: %lld\n"
													"keys: %lld\n"
													"to_sync: %lld\n"
													"sync_ok: %lld\n"
													"sync_err: %lld\n"
													"sync_interval_in_usec:%u\n"
													,
													sto_info.shards,
													sto_info.keys,
													sto_info.keys_to_sync,
													sto_info.sync_ok,
													sto_info.sync_err,
													db->opts->sync_interval_us
													);
	struct keyspace_info ks_info;
	swarmkv_keyspace_info(db->mod_keyspace, &ks_info);
	snprintf(ks_info_buff, sizeof(ks_info_buff), "# Keyspace\n"
															"health_check_port: %u\n"
															"slots: %lld\n"
															"keys: %lld\n"
															"expires: %lld\n"
															,
															ks_info.health_check_port,
															ks_info.slots,
															ks_info.keys,
															ks_info.expires);
	struct snet_info net_info;
	swarmkv_net_info(db->net, &net_info);
	snprintf(net_info_buff, sizeof(net_info_buff), "# Network\n"
												"connections: %lld\n"
												"pending_rpcs: %lld\n"
												"timed_out_rpcs: %lld\n"
												"input_bytes: %lld\n"
												"output_bytes: %lld\n"
												"input_cmds: %lld\n"
												"output_cmds: %lld\n"
												"input_replies: %lld\n"
												"output_replies: %lld\n"
												"input_msgs: %lld\n"
												"output_msgs: %lld\n"
												"instantaneous_input_kbps: %.2f\n"
												"instantaneous_output_kbps: %.2f\n"
												,
												net_info.connections,
												net_info.pending_rpcs,
												net_info.timed_out_rpcs,
												net_info.input_bytes,
												net_info.output_bytes,
												net_info.input_cmds,
												net_info.output_cmds,
												net_info.input_replies,
												net_info.output_replies,
												net_info.input_msgs,
												net_info.output_msgs,
												net_info.instantaneous_input_kbps,
												net_info.instantaneous_output_kbps
												);

	if(cmd->argc>1)
	{
		if(0==strcasecmp("Node", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_verbatim(node_info_buff, strlen(node_info_buff)+1, "txt");
		}
		else if(0==strcasecmp("Store", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_verbatim(store_info_buff, strlen(store_info_buff), "txt");			
		}
		else if(0==strcasecmp("Keyspace", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_verbatim(ks_info_buff, strlen(ks_info_buff), "txt");			
		}
		else if(0==strcasecmp("Network", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_verbatim(net_info_buff, strlen(net_info_buff), "txt");			
		}
		else
		{
			*reply=swarmkv_reply_new_verbatim("", 1, "txt");
		}
	}
	else
	{
		snprintf(all_info_buff, sizeof(all_info_buff), "%s\n%s\n%s\n%s",
											node_info_buff,
											store_info_buff,
											ks_info_buff,
											net_info_buff);
		*reply=swarmkv_reply_new_verbatim(all_info_buff, strlen(all_info_buff), "txt");
	}

	return FINISHED;
}
enum cmd_exec_result ping_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
	*reply=swarmkv_reply_new_string_fmt("PONG");
	return FINISHED;
}
enum cmd_exec_result __attribute__((optimize("O0"))) debug_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
    /*DEBUG SLEEP, ASSERT*/
	if(0==strcasecmp(cmd->argv[1], "sleep") && cmd->argc>2)
	{
		if(cmd->argc<3)
		{
			*reply=swarmkv_reply_new_error(error_need_additional_arg, cmd->argv[1]);
		}
		else
		{
			double dtime = strtod(cmd->argv[2], NULL);
			long long utime = dtime*1000000;
			struct timespec tv;

			tv.tv_sec = utime / 1000000;
			tv.tv_nsec = (utime % 1000000) * 1000;
			nanosleep(&tv, NULL);
			*reply=swarmkv_reply_new_status("OK");
    	}
	}
	else if(0==strcasecmp(cmd->argv[1], "assert"))
	{
		assert(0);
	}
	else
	{
		*reply=swarmkv_reply_new_error(error_arg_string_should_be, "sleep|assert");
	}
	return FINISHED;
}

enum cmd_exec_result config_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, const node_t *accessing_node, struct swarmkv_reply **reply)
{
	return FINISHED;
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

void cmd_exec_cb(const node_t *peer, const void *data, size_t size, void *arg)
{
	struct swarmkv_reply *reply=NULL;
	char error_str[256]={0};
	struct future *f = (struct future *) arg;
	struct promise *p = future_to_promise(f);
	if(data && size)
	{
		reply=swarmkv_deserialize_reply(data, size);		
		promise_success(p, (void*)reply);
		swarmkv_reply_free(reply);
	}
	else
	{
		snprintf(error_str, sizeof(error_str), "peer %s timeout", peer->addr);		
		promise_failed(p, FUTURE_ERROR_TIMEOUT, error_str);
	}
}

void exec_command_at_peer(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *peer, struct future* f)
{
	char *blob=NULL;
	size_t blob_sz=0;
	swarmkv_msg_serialize_cmd(cmd, &blob, &blob_sz);
	swarmkv_net_send_cmd(db->net, peer, blob, blob_sz, cmd_exec_cb, f);
	free(blob);	
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

static struct swarmkv_cmd *make_keyroute_cmd(enum cmd_key_flag flag, const sds key, const node_t *obj_owner)
{
	struct swarmkv_cmd *keyroute_cmd=NULL;
	switch(flag)
	{
		case CMD_KEY_RO:
		case CMD_KEY_RW:
			if(obj_owner)
			{
				keyroute_cmd=swarmkv_cmd_new(4);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("xradd");
				keyroute_cmd->argv[2]=sdsdup(key);
				keyroute_cmd->argv[3]=node_addr2sds(obj_owner);
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
				keyroute_cmd=swarmkv_cmd_new(4);
				keyroute_cmd->argv[0]=sdsnew("keyspace");
				keyroute_cmd->argv[1]=sdsnew("radd");
				keyroute_cmd->argv[2]=sdsdup(key);
				keyroute_cmd->argv[3]=node_addr2sds(obj_owner);
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
	node_t peer;
	if(reply->type==SWARMKV_REPLY_NODE && 0==strncasecmp(reply->str, "-ASK", 4))
	{
		ctx->redirect_cnt++;
		if(ctx->redirect_cnt<=3)
		{
			node_init_from_reply(&peer, reply);
			exec_command_at_peer(ctx->db, ctx->cmd, &peer, ctx->future_of_mine);
		}
		else
		{
			struct promise *p=future_to_promise(ctx->future_of_caller);
			char err_msg[256];
			snprintf(err_msg, sizeof(err_msg), error_too_many_redirects, reply->str);
			promise_failed(p, FUTURE_ERROR_EXCEPTION, err_msg);
			cmd_ctx_free(ctx);
		}
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
	node_t *replica_nodes=NULL;

	node_list_new_from_reply(&replica_nodes, &n_replica_node, reply);
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
		int self_is_a_replica=node_list_exists(replica_nodes, n_replica_node, &ctx->db->self);
		if(self_is_a_replica)
		{
			n_replica_node-=node_list_remove(replica_nodes, n_replica_node, &ctx->db->self);
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

void __exec_cmd(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *target_node, struct future *future_of_caller)
{
	struct swarmkv_cmd_spec *spec=NULL;
	struct swarmkv_reply *reply=NULL;
	enum cmd_exec_result exec_ret=FINISHED;
	node_t peer;
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
	if(!spec->auto_route && !target_node)
	{
		reply=swarmkv_reply_new_error(error_no_target_node, spec->name);
		p=future_to_promise(future_of_caller);
		promise_success(p, reply);
		swarmkv_reply_free(reply);
		return;
	}
	ctx=cmd_ctx_new(db, cmd, spec, future_of_caller);
	if(target_node && node_compare(&db->self, target_node))
	{
		ctx->future_of_mine=future_create("peer_exec", peer_exec_on_success, generic_on_fail, ctx);
		exec_command_at_peer(ctx->db, ctx->cmd, target_node, ctx->future_of_mine);
		return;
	}
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &start);
	spec_lock_module(spec, cmd);
	exec_ret=spec->proc(spec->module, cmd, NULL, &reply);
	spec_unlock_module(spec, cmd);
	clock_gettime(CLOCK_MONOTONIC_COARSE, &end);
	swarmkv_monitor_record_command(db->mod_monitor, spec->name, timespec_diff_usec(&start, &end));
	switch(exec_ret)
	{
		case FINISHED:
			p=future_to_promise(future_of_caller);
			promise_success(p, reply);
			cmd_ctx_free(ctx);
			ctx=NULL;
			break;
		case REDIRECT:
			ret=node_init_from_reply(&peer, reply);
			assert(ret==0);
			ctx->future_of_mine=future_create("peer_exec", peer_exec_on_success, generic_on_fail, ctx);
			exec_command_at_peer(ctx->db, ctx->cmd, &peer, ctx->future_of_mine);
			break;
		case NEED_KEY_ROUTE:
			keyspace_cmd=make_keyroute_cmd(spec->flag, cmd->argv[spec->key_offset], db->opts->dryrun? NULL:(&db->self));
			ctx->future_of_mine=future_create("key_route", key_route_on_success, generic_on_fail, ctx);
			__exec_cmd(db, keyspace_cmd, NULL, ctx->future_of_mine);
			swarmkv_cmd_free(keyspace_cmd);
			keyspace_cmd=NULL;
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

void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *target_node, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	struct caller_ctx *ctx=NULL;
	ctx=ALLOC(struct caller_ctx, 1);
	ctx->cb=cb;
	ctx->cb_arg=cb_arg;
	ctx->my_future=future_create("exec_cmd", exec_cmd_on_success, exec_cmd_on_fail, ctx);
	__exec_cmd(db, cmd, target_node, ctx->my_future);

	return;
}

void on_cmd_msg_callback(const node_t *peer, unsigned long long sequence, const void *data, size_t size, void *arg)
{
	struct swarmkv* db=(struct swarmkv*)arg;
	struct swarmkv_cmd *cmd=NULL;
	cmd=swarmkv_msg_deserialize_cmd(data, size);

	struct swarmkv_reply* reply=NULL;
	struct swarmkv_cmd_spec *spec=NULL;
	spec=get_spec_by_argv(db, cmd->argc, cmd->argv);
	if(spec)
	{
		if(is_sufficient_arg_num(spec, cmd))
		{
			struct timespec start, end;
			clock_gettime(CLOCK_MONOTONIC_COARSE, &start);

			spec_lock_module(spec, cmd);
			spec->proc(spec->module, cmd, peer, &reply);
			spec_unlock_module(spec, cmd);

			clock_gettime(CLOCK_MONOTONIC_COARSE, &end);
			swarmkv_monitor_record_command(db->mod_monitor, spec->name, timespec_diff_usec(&start, &end));

		}
		else
		{
			reply=swarmkv_reply_new_error(error_wrong_number_of_arg, spec->name);
		}
	}
	else
	{
		reply=swarmkv_reply_new_error("Unknown command %d", cmd->argv[0]);
		log_fatal(db->logger, MODULE_SWAMRKV_CORE, "Unknown command %d", cmd->argv[0]);
	}
	char *reply_blob=NULL;
	size_t reply_blob_sz=0;
	swarmkv_msg_serialize_reply(reply, &reply_blob, &reply_blob_sz);
	swarmkv_net_send_reply(db->net, peer, reply_blob, reply_blob_sz, sequence);
	free(reply_blob);

	swarmkv_cmd_free(cmd);
	cmd=NULL;
	swarmkv_reply_free(reply);
	reply=NULL;
	return;
}

void command_register(struct swarmkv_cmd_spec **table, const char *name, const char *hint,
						int arity, int key_offset, enum cmd_key_flag flag, int auto_route,
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
	spec->auto_route=auto_route;

	spec->module=module;
	HASH_ADD_KEYPTR(hh, *table, spec->name, strlen(spec->name), spec);
	return;
}
void command_spec_init(struct swarmkv *db)
{
	int AUTO_ROUTE=1, NOT_AUTO_ROUTE=0;
	/* String and Integer commands*/
	command_register(&(db->command_table), "GET", "key",
					1, 1, CMD_KEY_RO, AUTO_ROUTE,
					get_command, db->mod_store);
	command_register(&(db->command_table), "SET", "key value",
					2, 1, CMD_KEY_OW, AUTO_ROUTE,
					set_command, db->mod_store);
	command_register(&(db->command_table), "INCRBY", "key increment",
					2, 1, CMD_KEY_OW, AUTO_ROUTE,
					incrby_command, db->mod_store);

	/* Generic commands*/
					
	command_register(&(db->command_table), "DEL", "key",
					1, 1, CMD_KEY_RM, AUTO_ROUTE,
					del_command, db->mod_keyspace);
	command_register(&(db->command_table), "EXPIRE", "key seconds",
					2, 1, CMD_KEY_RW, AUTO_ROUTE,
					expire_command, db->mod_keyspace);
	command_register(&(db->command_table), "TTL", "key",
					1, 1, CMD_KEY_RO, AUTO_ROUTE,
					ttl_command, db->mod_keyspace);
	command_register(&(db->command_table), "PERSIST", "key",
					1, 1, CMD_KEY_RW, AUTO_ROUTE,
					persist_command, db->mod_keyspace);

	/* Set commands */
	command_register(&(db->command_table), "SADD", "key member [member ...]",
					2, 1, CMD_KEY_OW, AUTO_ROUTE,
					sadd_command, db->mod_store);
	command_register(&(db->command_table), "SREM", "key member [member ...]",
					2, 1, CMD_KEY_RW, AUTO_ROUTE,
					srem_command, db->mod_store);
	command_register(&(db->command_table), "SMEMBERS", "key",
					1, 1, CMD_KEY_RO, AUTO_ROUTE,
					smembers_command, db->mod_store);
	command_register(&(db->command_table), "SISMEMBER", "key member",
					2, 1, CMD_KEY_RO, AUTO_ROUTE,
					sismember_command, db->mod_store);
	command_register(&(db->command_table), "SCARD", "key",
					1, 1, CMD_KEY_RO, AUTO_ROUTE,
					scard_command, db->mod_store);

	/* Token bucket commands */
	command_register(&(db->command_table), "TCFG", "key capacity rate",
					3, 1, CMD_KEY_OW, AUTO_ROUTE,
					tcfg_command, db->mod_store);
	command_register(&(db->command_table), "TCONSUME", "key tokens [NORMAL|FORCE|FLEXIBLE]",
					2, 1, CMD_KEY_RW, AUTO_ROUTE,
					tconsume_command, db->mod_store);
	command_register(&(db->command_table), "TINFO", "key",
					1, 1, CMD_KEY_RO, AUTO_ROUTE,
					tinfo_command, db->mod_store);

	/* Information Commands */
	command_register(&(db->command_table), "INFO", "[section]",
					0, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					info_command, &db->module);
	command_register(&(db->command_table), "DEBUG", "SLEEP|CRASH",
					1, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					debug_command, &db->module);
	command_register(&(db->command_table), "PING", "",
					0, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					ping_command, &db->module);

	command_register(&(db->command_table), "LATENCY COMMAND", "[command]",
					0, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					latency_command_command, db->mod_monitor);
	command_register(&(db->command_table), "LATENCY PEER", "[IP:port]",
					0, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					latency_peer_command, db->mod_monitor);
	command_register(&(db->command_table), "LATENCY EVENT", "[event_name]",
					0, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					latency_event_command, db->mod_monitor);

	/* low-level state-based CRDT synchronization commands*/
	command_register(&(db->command_table), "CRDT PULL", "key",
					1, 2, CMD_KEY_NA, NOT_AUTO_ROUTE,
					crdt_pull_command, db->mod_store);
	command_register(&(db->command_table), "CRDT PUSH", "key blob",
					2, 2, CMD_KEY_NA, NOT_AUTO_ROUTE,
					crdt_push_command, db->mod_store);
	command_register(&(db->command_table), "CRDT JOIN", "key IP:port",
					2, 2, CMD_KEY_NA, NOT_AUTO_ROUTE,
					crdt_join_command, db->mod_store);

	command_register(&(db->command_table), "CRDT DEL", "key",
					1, 2, CMD_KEY_NA, NOT_AUTO_ROUTE,
					crdt_del_command, db->mod_store);
    command_register(&(db->command_table), "CRDT KEYS", "pattern",
					1, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					crdt_keys_command, db->mod_store);
    command_register(&(db->command_table), "CRDT EXISTS", "key",
					1, 2, CMD_KEY_RO, NOT_AUTO_ROUTE,
					crdt_exists_command, db->mod_store);

     command_register(&(db->command_table), "CRDT RLIST", "key",
					1, 2, CMD_KEY_RO, NOT_AUTO_ROUTE,
					crdt_rlist_command, db->mod_store);

	/* low-level keyspace operation commands */
	command_register(&(db->command_table), "KEYSPACE RLIST", "key",
					1, 2, CMD_KEY_RO, AUTO_ROUTE,
					keyspace_rlist_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE RADD", "key IP:port",
					1, 2, CMD_KEY_OW, AUTO_ROUTE,
					keyspace_radd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE XRADD", "key IP:port",
					1, 2, CMD_KEY_OW, AUTO_ROUTE,
					keyspace_xradd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE KEYS", "pattern",
					1, -1, CMD_KEY_RO, NOT_AUTO_ROUTE,
					keyspace_keys_command, db->mod_keyspace);
    command_register(&(db->command_table), "KEYSPACE RDEL", "key IP:port",
					1, 2, CMD_KEY_RO, AUTO_ROUTE,
					keyspace_rdel_command, db->mod_keyspace);

	/* low-level keyspace reorgnization commands */
	command_register(&(db->command_table), "KEYSPACE SETSLOT", "<slot> IMPORTING|MIGRATING|NODE|STABLE IP:port",
					2, -1, CMD_KEY_NA, NOT_AUTO_ROUTE,
					keyspace_setslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE GETKEYSINSLOT", "slot",
					1, -1, CMD_KEY_NA, NOT_AUTO_ROUTE,
					keyspace_getkeysinslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE ADDKEYSTOSLOT", "slot blob",
					2, -1, CMD_KEY_NA, NOT_AUTO_ROUTE,
					keyspace_addkeystoslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE DELSLOTKEYS", "slot",
					1, -1, CMD_KEY_NA, NOT_AUTO_ROUTE,
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

	db=ALLOC(struct swarmkv, 1);
	strncpy(db->db_name, db_name, sizeof(db->db_name));

	strncpy(db->module.name, "db", sizeof(db->module.name));
	db->module.mod_ctx=db;
	db->module.lock=NULL;
	db->module.unlock=NULL;

	db->opts=opts;
	if(opts->logger)
	{
		db->logger=opts->logger;
	}
	else
	{
		char log_path[1024]="";
		snprintf(log_path, sizeof(log_path), "%s.log", db_name);
        db->logger=log_handle_create(log_path, opts->loglevel);
	}
	swarmkv_create_thread_ctx(db);

	uuid_copy(db->bin_uuid, opts->bin_uuid);
	if(opts->dryrun)
	{
	}
	node_init(&db->self, opts->bind_address, opts->p2p_port);
	db->mod_monitor=swarmkv_monitor_new(db->opts);

	//Note: if the p2p_port is 0, swarmkv_net_new updates db->self.p2p_port.
	db->net=swarmkv_net_new(opts, &db->self, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_net_set_on_cmd_cb(db->net, on_cmd_msg_callback, db);
	swarmkv_net_set_monitor_handle(db->net, db->mod_monitor);

	db->mod_keyspace=swarmkv_keyspace_new(db->opts, db_name, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_keyspace_set_exec_cmd_func(db->mod_keyspace, __exec_cmd, db);
	swarmkv_keyspace_set_monitor_handle(db->mod_keyspace, db->mod_monitor);

	db->mod_store=swarmkv_store_new(db->opts, __exec_cmd, db, opts->bin_uuid);
	swarmkv_net_timer_add(db->net, 0, opts->sync_interval_us, 1, (timeout_cb*)swarmkv_store_periodic, db->mod_store);
	swarmkv_store_set_monitor_handle(db->mod_store, db->mod_monitor);

	command_spec_init(db);

	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		swarmkv_monitor_register_commands(db->mod_monitor, spec->name);
	}
	
	swarmkv_threads_run(db);
	char str_uuid[37]="";
	uuid_unparse(db->opts->bin_uuid, str_uuid);
	opts->is_assigned_to_db=1;
	clock_gettime(CLOCK_MONOTONIC, &db->boot_time);
	log_info(db->logger, MODULE_SWAMRKV_CORE, "swarmkv instance %s of cluster %s is opened.", str_uuid, db->db_name);
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
	for(size_t i=0; i<db->opts->nr_worker_threads; i++)
	{
		pthread_join(db->worker_threads[i]->thr, NULL);
	}
	swarmkv_store_free(db->mod_store);
	db->mod_store=NULL;
	swarmkv_keyspace_free(db->mod_keyspace);
	db->mod_keyspace=NULL;
	
	swarmkv_net_free(db->net);
	db->net=NULL;

	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		HASH_DEL(db->command_table, spec);
		free(spec);
	}

	log_info(db->logger, MODULE_SWAMRKV_CORE, "swarmkv cluster %s closed.", db->db_name);
	if(db->opts->logger != db->logger)
	{
		log_handle_destroy(db->logger);
		db->logger=NULL;
	}
	free(db->opts);
	db->opts=NULL;
	free(db);
	return;
}



