#include "swarmkv/swarmkv.h"

#include "swarmkv_common.h"
#include "swarmkv_cmd_spec.h"
#include "swarmkv_limits.h"
#include "swarmkv_utils.h"
#include "swarmkv_error.h"

//header of communications
#include "swarmkv_message.h"
#include "future_promise.h"
#include "swarmkv_rpc.h"
#include "swarmkv_mesh.h"
#include "swarmkv_net.h"

//header of modules
#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_monitor.h"

//header of data types
#include "t_string.h"
#include "t_set.h"
#include "t_token_bucket.h"
#include "t_hash.h"



#include "uthash.h"
#include "sds.h"
#include "log.h"

#include <stdio.h>
#include <uuid/uuid.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <ctype.h>
#include <stdatomic.h>


#define	MODULE_SWAMRKV_CORE	module_name_str("swarmkv.core")



struct swarmkv_thread
{
	pthread_t thr;
	int sys_tid;
	int thread_id;
	int recusion_depth;
	struct swarmkv *db;
	struct event_base *evbase;
	int is_dispatching;
	time_t lastime;
	struct event *store_periodic_ev;
};


struct swarmkv
{
	struct swarmkv_module module;
	char db_name[SWARMKV_SYMBOL_MAX];
	uuid_t bin_uuid;
	struct swarmkv_options *opts;

	int thread_counter;
	struct swarmkv_thread *threads;
	pthread_barrier_t barrier;

	struct event_base **ref_evbases;

	node_t self;
	struct swarmkv_rpc_mgr *rpc_mgr;
	struct swarmkv_mesh *mesh;
	struct swarmkv_net *net;
	
	struct log_handle *logger;
	struct swarmkv_module *mod_keyspace;
	struct swarmkv_module *mod_store;
	struct swarmkv_module *mod_monitor;
	struct swarmkv_cmd_spec *command_table;
	struct timespec boot_time;

	//For statistics
	long long local_cmds;
	long long remote_cmds;
};
struct swarmkv *module2db(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "db"));
	struct swarmkv *db=container_of(module, struct swarmkv, module);
	assert(db==module->mod_ctx);
	return db;
}
__thread int __sys_tid=-1;
void swarmkv_register_thread(struct swarmkv *db)
{
	int thread_id = atomic_fetch_add(&db->thread_counter, 1);
	assert(thread_id < db->opts->nr_worker_threads + db->opts->nr_caller_threads);
	if(__sys_tid<0) __sys_tid=syscall(SYS_gettid);
	db->threads[thread_id].sys_tid=__sys_tid;
	db->threads[thread_id].thread_id=thread_id;
	return;
}
struct swarmkv_options *swarmkv_get0_options(struct swarmkv *db)
{
	return db->opts;
}

int __gettid(struct swarmkv *db)
{
//	int __sys_tid=syscall(SYS_gettid);
	for(int i=0; i<db->opts->nr_worker_threads + db->opts->nr_caller_threads; i++)
	{
		if(db->threads[i].sys_tid==__sys_tid)
		{
			return db->threads[i].thread_id;
		}
	}
	assert(0);
	return -1;
}
void __exec_cmd(struct swarmkv *db, const node_t *target_node, const struct swarmkv_cmd *cmd, struct future *future_of_caller);
struct local_caller_ctx
{
	swarmkv_on_reply_callback_t * cb;
	void * cb_arg;
	struct future *my_future;
};
static void local_caller_on_success(void *result, void *user)
{	
	struct local_caller_ctx *ctx=(struct local_caller_ctx*)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply *)result;
	ctx->cb(reply, ctx->cb_arg);
	future_destroy(ctx->my_future);
	free(ctx);
}
static void local_caller_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct local_caller_ctx *ctx=(struct local_caller_ctx*)user;
	struct swarmkv_reply *reply=NULL;
	if(err==FUTURE_ERROR_CANCEL)
	{
		reply=swarmkv_reply_new_error("cancelled");
	}
	else
	{
		reply=swarmkv_reply_new_error(what);
	}
	ctx->cb(reply, ctx->cb_arg);
	future_destroy(ctx->my_future);
	free(ctx);
	swarmkv_reply_free(reply);
}
void exec_for_local(struct swarmkv *db, const struct swarmkv_cmd *cmd, const node_t *target_node, swarmkv_on_reply_callback_t * cb, void * cb_arg)
{
	struct local_caller_ctx *ctx=NULL;
	ctx=ALLOC(struct local_caller_ctx, 1);
	ctx->cb=cb;
	ctx->cb_arg=cb_arg;
	ctx->my_future=future_create("for_local", 
								local_caller_on_success,
								local_caller_on_fail,
								ctx);
	__exec_cmd(db, target_node, cmd, ctx->my_future);
	db->local_cmds++;
	return;
}
struct remote_caller_ctx
{
	struct future *my_future;
	struct swarmkv *db;
    long long sequence;
    int remote_tid;
    node_t remote;
};
void remote_calller_ctx_free(struct remote_caller_ctx *ctx)
{
	future_destroy(ctx->my_future);
	ctx->my_future=NULL;
	ctx->db=NULL;
	free(ctx);
	return;
}
static void remoter_caller_ctx_send_reply(struct remote_caller_ctx *ctx, const struct swarmkv_reply *reply)
{
	struct swarmkv *db = ctx->db;
	struct swarmkv_msg *msg=swarmkv_msg_new_by_reply(reply, &ctx->remote, ctx->remote_tid, &db->self, ctx->sequence);
	int cur_tid=__gettid(db);
	const char *err_str=NULL;
	if(0==node_compare(&db->self, &msg->caller))
	{
		//Thread can not send message to itself.
		assert(cur_tid != msg->caller_tid);
		swarmkv_mesh_send(db->mesh, cur_tid, msg->caller_tid, msg);
	}
	else
	{
		swarmkv_net_send(ctx->db->net, &ctx->remote, msg, &err_str);
	}	
	return;
}
static void remote_caller_on_success(void *result, void *user)
{
	struct remote_caller_ctx *ctx=(struct remote_caller_ctx*)user;
	const struct swarmkv_reply *reply=(const struct swarmkv_reply*) result;
	remoter_caller_ctx_send_reply(ctx, reply);
	remote_calller_ctx_free(ctx);
	return;
}
static void remote_caller_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct remote_caller_ctx *ctx=(struct remote_caller_ctx*)user;
	struct swarmkv_reply *reply=NULL;
	if(err==FUTURE_ERROR_CANCEL)
	{
		reply=swarmkv_reply_new_error("cancelled");
	}
	else
	{
		reply=swarmkv_reply_new_error(what);
		remoter_caller_ctx_send_reply(ctx, reply);
	}
	remote_calller_ctx_free(ctx);
	return;
}
void exec_for_remote(struct swarmkv *db, const struct swarmkv_msg *msg)
{
	struct remote_caller_ctx *ctx=ALLOC(struct remote_caller_ctx, 1);
	ctx->db=db;
	ctx->sequence=msg->sequence;
	ctx->remote_tid=msg->caller_tid;
	node_copy(&ctx->remote, &msg->caller);
	ctx->my_future=future_create("for_remote", 
								remote_caller_on_success,
								remote_caller_on_fail,
								ctx);
	__exec_cmd(db, &msg->executor, msg->cmd, ctx->my_future);
	db->remote_cmds++;
	//Todo: implement monitor command here
	return;
}
enum cmd_exec_result info_command(struct swarmkv_module *mod_db, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
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
	sds pending_cmds=sdsnew("[");
	size_t rpc_cnt=0;
	for(int i=0; i<db->opts->total_threads; i++)
	{
		rpc_cnt=swarmkv_rpc_mgr_count(db->rpc_mgr, i);
		pending_cmds=sdscatprintf(pending_cmds, "%zu", rpc_cnt);
		if(i==db->opts->nr_worker_threads-1)
		{
			pending_cmds=sdscatprintf(pending_cmds, "], [");
		}
		else if(i==db->opts->total_threads-1)
		{
			pending_cmds=sdscatprintf(pending_cmds, "]");
		}
		else
		{
			pending_cmds=sdscatprintf(pending_cmds, ", ");
		}
	}
	struct swarmkv_rpc_mgr_info rpc_info;
	swarmkv_rpc_mgr_info(db->rpc_mgr, &rpc_info);
	struct mesh_info mesh_info;
	swarmkv_mesh_info(db->mesh, &mesh_info);
	snprintf(node_info_buff, sizeof(node_info_buff), "# Node\r\n"
											"version: %s\r\n"
											"address: %s\r\n"
											"uuid: %s\r\n"
											"worker_threads: %d\r\n"
											"caller_threads: %d\r\n"
											"pending_cmds: %s\r\n"
											"local_cmds: %lld\r\n"
											"remote_cmds: %lld\r\n"
											"timed_out_cmds: %lld\r\n"
											"unknown_sequence: %lld\r\n"
											"mesh_queued_msgs: %lld\r\n"
											"mesh_drop_msgs: %lld\r\n"
											"server_time_usec: %lld\r\n"
											"up_time_in_seconds: %ld\r\n"
											"up_time_in_days: %ld\r\n"
											,
											SWARMKV_VERSION,
											db->self.addr,
											uuid_str,
											db->opts->nr_worker_threads,
											db->opts->nr_caller_threads,
											pending_cmds,
											db->local_cmds,
											db->remote_cmds,
											rpc_info.timed_out_rpcs,
											rpc_info.unknown_sequence,
											mesh_info.queued_msgs,
											mesh_info.enqueue_drops,
											server_time_us,
											now_monotonic.tv_sec-db->boot_time.tv_sec,
											(now_monotonic.tv_sec-db->boot_time.tv_sec)/(3600*24)
											);
	sdsfree(pending_cmds);
	struct store_info sto_info;
	swarmkv_store_info(db->mod_store, &sto_info);
	snprintf(store_info_buff, sizeof(store_info_buff), "# Store\r\n"
													"keys: %lld\r\n"
													"to_sync: %lld\r\n"
													"synced: %lld\r\n"
													"sync_ok: %lld\r\n"
													"sync_err: %lld\r\n"
													"sync_interval_in_msec: %.3f\r\n"
													,
													sto_info.keys,
													sto_info.keys_to_sync,
													sto_info.synced,
													sto_info.sync_ok,
													sto_info.sync_err,
													(double)db->opts->sync_interval_us/1000
													);
	struct keyspace_info ks_info;
	swarmkv_keyspace_info(db->mod_keyspace, &ks_info);
	snprintf(ks_info_buff, sizeof(ks_info_buff), "# Keyspace\r\n"
															"health_check_port: %u\r\n"
															"slots: %lld\r\n"
															"keys: %lld\r\n"
															"expires: %lld\r\n"
															,
															ks_info.health_check_port,
															ks_info.slots,
															ks_info.keys,
															ks_info.expires);
	struct snet_info net_info;
	swarmkv_net_info(db->net, &net_info);
	snprintf(net_info_buff, sizeof(net_info_buff), "# Network\r\n"
												"timeout_in_msec: %.3f\r\n"
												"connections: %lld\r\n"
												"input_bytes: %lld\r\n"
												"output_bytes: %lld\r\n"
												"input_msgs: %lld\r\n"
												"output_msgs: %lld\r\n"
												"input_error_msgs: %lld\r\n"
												"output_drop_msgs: %lld\r\n"
												"input_buffer: %lld\r\n"
												"output_buffer: %lld\r\n"
												"instantaneous_input_kbps: %.2f\r\n"
												"instantaneous_output_kbps: %.2f\r\n"
												"instantaneous_input_msgs: %.2f\r\n"
												"instantaneous_output_msgs: %.2f\r\n"
												,
												(double)db->opts->cluster_timeout_us/1000,
												net_info.connections,
												net_info.input_bytes,
												net_info.output_bytes,
												net_info.input_msgs,
												net_info.output_msgs,
												net_info.input_error_msgs,
												net_info.output_drop_msgs,
												net_info.input_buffer_sz,
												net_info.output_buffer_sz,
												net_info.instantaneous_input_kbps,
												net_info.instantaneous_output_kbps,
												net_info.instantaneous_input_msgs,
												net_info.instantaneous_output_msgs
												);

	if(cmd->argc>1)
	{
		if(0==strcasecmp("Node", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_status(node_info_buff);
		}
		else if(0==strcasecmp("Store", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_status(store_info_buff);
		}
		else if(0==strcasecmp("Keyspace", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_status(ks_info_buff);
		}
		else if(0==strcasecmp("Network", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_status(net_info_buff);
		}
		else if(0==strcasecmp("Threads", cmd->argv[1]))
		{
			*reply=swarmkv_reply_new_string_from_integer(db->opts->nr_worker_threads);
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
		*reply=swarmkv_reply_new_status(all_info_buff);
	}

	return FINISHED;
}
enum cmd_exec_result ping_command(struct swarmkv_module *mod_db, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv *db=module2db(mod_db);
	node_t target;
	node_init_from_sds(&target, cmd->argv[1]);
	if(0==node_compare(&target, &db->self))
	{
		*reply=swarmkv_reply_new_string_fmt("PONG from %s", swarmkv_self_address(db));
		return FINISHED;
	}
	else
	{
		*reply=swarmkv_reply_new_node(&target, 1);
		return REDIRECT;
	}
}
enum cmd_exec_result __attribute__((optimize("O0"))) debug_command(struct swarmkv_module *mod_db, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{

	if(cmd->argc==2 && !strcasecmp(cmd->argv[1], "help") )
	{
		const char *help = {
"DEBUG <subcommand> [<arg> [value] [opt] ...]. Subcommands are:\n"
"SLEEP <seconds>\n"
"    Stop the server for <seconds>. Decimals allowed.\n"
"ASSERT\n"
"    Crash by assertion failed.\n"
		};
		*reply=swarmkv_reply_new_status(help);
	}
	else if(!strcasecmp(cmd->argv[1], "sleep") && cmd->argc>2)
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
	else if(!strcasecmp(cmd->argv[1], "assert"))
	{
		assert(0);
	}
	else
	{
		*reply=swarmkv_reply_new_error(erorr_subcommand_syntax, cmd->argv[1], cmd->argv[0]);
	}
	return FINISHED;
}

enum cmd_exec_result config_command(struct swarmkv_module *mod_store, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	//Unused
	return FINISHED;
}
enum cmd_exec_result command_list_command(struct swarmkv_module *mod_db, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
	struct swarmkv *db=module2db(mod_db);
	size_t cnt=HASH_COUNT(db->command_table);
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	int i=0;
	*reply=swarmkv_reply_new_array(cnt);
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		(*reply)->elements[i]=swarmkv_reply_new_string(spec->name, strlen(spec->name));
		i++;
	}
	assert(i==cnt);
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


 __attribute__((unused)) static void libevent_log_cb(int severity, const char *msg)
{
    const char *s;
    FILE *logfile=fopen("libevent_run.log", "a");
    switch (severity) {
        case _EVENT_LOG_DEBUG: s = "debug"; break;
        case _EVENT_LOG_MSG:   s = "msg";   break;
        case _EVENT_LOG_WARN:  s = "warn";  break;
        case _EVENT_LOG_ERR:   s = "error"; break;
        default:               s = "?";     break; // never reached
    }
    fprintf(logfile, "[%s] %s\n", s, msg);
	fclose(logfile);
	sleep(1000);
}
void __swarmkv_periodic(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_thread *thr=(struct swarmkv_thread *)arg;
	
	swarmkv_store_periodic(thr->db->mod_store, thr->thread_id);
	swarmkv_keyspace_periodic(thr->db->mod_keyspace, thr->thread_id);
}
void *swarmkv_worker_thread(void *arg)
{
	struct swarmkv *db = (struct swarmkv *)arg;
	swarmkv_register_thread(db);
	int tid=__gettid(db);
	struct swarmkv_thread *thr = db->threads+tid;
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv-%u", thr->thread_id);
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);

	struct timeval sync_interval = {db->opts->sync_interval_us/(1000*1000), db->opts->sync_interval_us%(1000*1000)};
	struct event * periodic_ev=event_new(thr->evbase, -1, EV_PERSIST, __swarmkv_periodic, thr);
	evtimer_add(periodic_ev, &sync_interval);
	
	thr->is_dispatching=1;
	pthread_barrier_wait(&db->barrier);

	
	int ret=event_base_dispatch(thr->evbase);
	event_del(periodic_ev);
	event_free(periodic_ev);

	if(thr->is_dispatching)
	{
		log_fatal(db->logger, MODULE_SWAMRKV_CORE, "worker thread event_base_dispatch() exit abnormally, ret=%d", ret);
	}
	else
	{
		log_info(thr->db->logger, MODULE_SWAMRKV_CORE, "%s worker thread %d exited", thr->db->db_name, thr->sys_tid);
	}	
	return NULL;
}

void swarmkv_threads_run(struct swarmkv *db)
{
	pthread_barrier_init(&db->barrier, NULL, db->opts->nr_worker_threads+1);
	int i = 0, ret=0;
	for (i = 0; i < db->opts->nr_worker_threads; i++)
	{
		ret=pthread_create(&db->threads[i].thr, NULL, swarmkv_worker_thread, db);
		if(ret !=0 )//error
		{
			log_fatal(db->logger, MODULE_SWAMRKV_CORE, "pthread_create() error %d", ret);
		}
	}
	pthread_barrier_wait(&db->barrier);
	return;
}

static struct swarmkv_reply *key_not_found_reply(enum key_not_found_reply not_found_flag)
{
	struct swarmkv_reply *reply=NULL;
	switch(not_found_flag)
	{
		case REPLY_INT_0:
			reply=swarmkv_reply_new_integer(0);		
			break;
		case REPLY_INT_MINORS1:
			reply=swarmkv_reply_new_integer(-1);
			break;
		case REPLY_NIL:
			reply=swarmkv_reply_new_nil();
			break;
		case REPLY_EMPTY_ARRAY:
			reply=swarmkv_reply_new_array(0);
			break;
		case REPLY_STR_NONE:
			reply=swarmkv_reply_new_string_fmt("none");
			break;
		case REPLY_ERROR:
			reply=swarmkv_reply_new_error(error_keyspace_obj_owner_not_found);
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
	struct swarmkv_cmd *cmd;
	int redirect_cnt;
	struct future *future_of_mine;
	struct future *future_of_caller;
};

struct cmd_ctx *cmd_ctx_new(struct swarmkv *db, const struct swarmkv_cmd *cmd, struct future *f)
{
	struct cmd_ctx *ctx=ALLOC(struct cmd_ctx, 1);
	ctx->db=db;
	ctx->cmd=swarmkv_cmd_dup(cmd);
	ctx->redirect_cnt=0;
	ctx->future_of_caller=f;
	return ctx;
}
void cmd_ctx_free(struct cmd_ctx *ctx)
{
	swarmkv_cmd_free(ctx->cmd);
	future_destroy(ctx->future_of_mine);
	free(ctx);
	return;
}
static void generic_on_fail(enum e_future_error err, const char * what, void * user)
{
	struct cmd_ctx *ctx=(struct cmd_ctx*)user;
	if(ctx->future_of_caller)
	{
		struct promise *p=future_to_promise(ctx->future_of_caller);
		promise_failed(p, err, what);
	}
	cmd_ctx_free(ctx);
}
static void peer_exec_on_success(void *result, void *user)
{	
	struct cmd_ctx *ctx = (struct cmd_ctx*)user;
	struct swarmkv_reply *reply = (struct swarmkv_reply*) result;
	
	if(reply->type==SWARMKV_REPLY_NODE && 0==strncasecmp(reply->str, "-ASK", 4))
	{
		ctx->redirect_cnt++;
		if(ctx->redirect_cnt<=3)
		{
			node_t target_node;
			node_init_from_reply(&target_node, reply);
			__exec_cmd(ctx->db, &target_node, ctx->cmd, ctx->future_of_mine);
		}
		else
		{
			char err_msg[256];
			snprintf(err_msg, sizeof(err_msg), error_too_many_redirects, reply->str);
			struct promise *p=future_to_promise(ctx->future_of_caller);
			promise_failed(p, FUTURE_ERROR_EXCEPTION, err_msg);
			cmd_ctx_free(ctx);
		}
	}
	else
	{
		struct promise *p=future_to_promise(ctx->future_of_caller);
		promise_success(p, (void*) reply);
		cmd_ctx_free(ctx);
	}
}
struct swarmkv_cmd *make_crdt_add_cmd(const sds key, node_t replica[], size_t n_replica)
{
	struct swarmkv_cmd *crdt_add_cmd=NULL;
	crdt_add_cmd=swarmkv_cmd_new(3+n_replica);
	crdt_add_cmd->argv[0]=sdsnew("crdt");
	crdt_add_cmd->argv[1]=sdsnew("add");
	crdt_add_cmd->argv[2]=sdsdup(key);
	for(size_t i=0; i<n_replica; i++)
	{
		crdt_add_cmd->argv[3+i]=node_addr2sds(replica+i);
	}
	return crdt_add_cmd;
}
static void crdt_add_on_success(void *result, void *user)
{
	struct swarmkv_reply *reply=(struct swarmkv_reply*)result;
	struct cmd_ctx *ctx = (struct cmd_ctx*)user;
	assert(reply->type==SWARMKV_REPLY_STATUS);
	if(ctx->future_of_caller)
	{
		__exec_cmd(ctx->db, NULL, ctx->cmd, ctx->future_of_caller);
	}
	cmd_ctx_free(ctx);
}
static void key_route_on_success(void *result, void *user)
{
	struct cmd_ctx *ctx = (struct cmd_ctx*)user;
	const struct swarmkv_reply *reply = (const struct swarmkv_reply*) result;
	struct swarmkv_reply *user_reply_for_keyspace_not_found=NULL;
	size_t n_replica_node=0;
	node_t *replica_nodes=NULL;
	struct swarmkv_cmd_spec *spec=get_spec_by_argv(ctx->db, ctx->cmd->argc, ctx->cmd->argv);

	node_list_new_from_reply(&replica_nodes, &n_replica_node, reply);
	if(n_replica_node==0)
	{
		user_reply_for_keyspace_not_found=key_not_found_reply(spec->nokey_reply);
		struct promise *p=future_to_promise(ctx->future_of_caller);
		promise_success(p, user_reply_for_keyspace_not_found);
		swarmkv_reply_free(user_reply_for_keyspace_not_found);
		user_reply_for_keyspace_not_found=NULL;
	}
	else
	{
		const sds key=ctx->cmd->argv[spec->key_offset];
		int self_is_a_replica=node_list_exists(replica_nodes, n_replica_node, &ctx->db->self);
		if(self_is_a_replica)
		{
			n_replica_node-=node_list_remove(replica_nodes, n_replica_node, &ctx->db->self);
		}
		if(n_replica_node>0)
		{
			__exec_cmd(ctx->db,  replica_nodes+0, ctx->cmd, ctx->future_of_caller);
			//__send_cmd(ctx->db, replica_nodes+0, ctx->cmd, ctx->future_of_caller);
		}
		if(self_is_a_replica)
		{
			struct cmd_ctx *crdt_add_ctx=NULL;
			struct swarmkv_cmd *crdt_add_cmd=make_crdt_add_cmd(key, replica_nodes, n_replica_node);
			crdt_add_ctx=cmd_ctx_new(ctx->db, ctx->cmd, n_replica_node>0?NULL:ctx->future_of_caller);
			crdt_add_ctx->future_of_mine=future_create("crdt_add", crdt_add_on_success, generic_on_fail, crdt_add_ctx);
			__exec_cmd(ctx->db, &ctx->db->self, crdt_add_cmd, crdt_add_ctx->future_of_mine);
			swarmkv_cmd_free(crdt_add_cmd);
		}
		free(replica_nodes);
	}
	cmd_ctx_free(ctx);
}

static int spec_gettid(struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd, int nr_worker_threads)
{
	int tid=0;
	switch(spec->key_offset)
	{
		case KEY_OFFSET_TID:
			tid=atoi(cmd->argv[2]);
			break;
		case KEY_OFFSET_SLOTID:
			tid=swarmkv_keyspace_slot2tid(spec->module, atoi(cmd->argv[2]));
			break;
		default:
			tid=key2tid(cmd->argv[spec->key_offset], nr_worker_threads);
			break;
	}
	return tid;
}
#define MONITOR_INTER_THREAD_RPC	"inter-thread-rpc"
void __on_msg_callback(struct swarmkv_msg *msg, void *arg)
{
	struct swarmkv *db = (struct swarmkv *)arg;
	int cur_tid=__gettid(db);
	if(msg->type==MSG_TYPE_CMD)
	{
		//command may be from other thread or other node.
		assert(cur_tid < db->opts->nr_worker_threads);
		exec_for_remote(db, msg);
		swarmkv_msg_free(msg);
	}
	else//MSG_TYPE_REPLY
	{
		if(msg->caller_tid!=cur_tid)
		{
			//The msg's onwership is transfered to swarmkv_mesh_send.
			swarmkv_mesh_send(db->mesh, cur_tid, msg->caller_tid, msg);
		}
		else
		{
			long long latency_us=-1;
			latency_us=swarmkv_rpc_complete(db->rpc_mgr, cur_tid, msg->sequence, msg->reply);
			if(latency_us>=0)
			{
				if(node_compare(&db->self, &msg->executor))
				{
					swarmkv_monitor_record_peer(db->mod_monitor, &msg->executor, latency_us, cur_tid);
				}
				else
				{
					swarmkv_monitor_record_event(db->mod_monitor, MONITOR_INTER_THREAD_RPC, latency_us);
				}
			}
			swarmkv_msg_free(msg);
		}
	}
}
#define	INTER_THREAD_RPC_TIMEOUT_AHEAD	1000
void __exec_cmd(struct swarmkv *db, const node_t *target_node, const struct swarmkv_cmd *cmd, struct future *future_of_caller)
{
	struct swarmkv_cmd_spec *spec=NULL;	
	struct swarmkv_reply *reply=NULL;
	struct promise *p=NULL;
	int cur_tid=__gettid(db);
	
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

	int target_tid=spec_gettid(spec, cmd, db->opts->nr_worker_threads);
	
	struct swarmkv_msg *msg=NULL;	
	if(!node_is_empty(target_node) && node_compare(&db->self, target_node))
	{
		//cmd is executed in target node's on_msg_callback
		struct swarmkv_rpc *rpc=swarmkv_rpc_launch(db->rpc_mgr, cur_tid, future_of_caller);
		long long sequence=swarmkv_rpc_get_sequence(rpc);
		msg=swarmkv_msg_new_by_cmd(cmd, &db->self, cur_tid, target_node, sequence);
		int ret=0;
		if(cur_tid >= db->opts->nr_worker_threads)
		{
			//Only worker threads can do network communication, so command should be executed at worker thread first.
			ret=swarmkv_mesh_send(db->mesh, cur_tid, random()%db->opts->nr_worker_threads, msg);
			if(ret<0)
			{
				reply=swarmkv_reply_new_error(error_thread_rpc_buffer_full);
			}
			swarmkv_rpc_set_timeout(rpc, db->opts->cluster_timeout_us+INTER_THREAD_RPC_TIMEOUT_AHEAD);
		}
		else
		{
			const char *err_str=NULL;
			ret=swarmkv_net_send(db->net, target_node, msg, &err_str);
			if(ret<0)
			{
				reply=swarmkv_reply_new_error(error_network_error, node_addr2sds(target_node), err_str);
			}
			swarmkv_rpc_set_peer(rpc, target_node);
		}
		if(reply)
		{
			swarmkv_rpc_complete(db->rpc_mgr, cur_tid, cur_tid, reply);
			swarmkv_reply_free(reply);
		}
		return;
	}
	if(cur_tid != target_tid)
	{
		//cmd will be executed in target thread's on_msg_callback
		struct swarmkv_rpc *rpc=swarmkv_rpc_launch(db->rpc_mgr, cur_tid, future_of_caller);
		swarmkv_rpc_set_timeout(rpc, db->opts->cluster_timeout_us+INTER_THREAD_RPC_TIMEOUT_AHEAD);
		long long sequence=swarmkv_rpc_get_sequence(rpc);
		msg=swarmkv_msg_new_by_cmd(cmd, &db->self, cur_tid, target_node, sequence);
		int ret=0;
		ret=swarmkv_mesh_send(db->mesh, cur_tid, target_tid, msg);
		if(ret<0)
		{
			reply=swarmkv_reply_new_error(error_thread_rpc_buffer_full);
			swarmkv_rpc_complete(db->rpc_mgr, cur_tid, cur_tid, reply);
			swarmkv_reply_free(reply);
		}
		return;
	}
	db->threads[cur_tid].recusion_depth++;
	assert(db->threads[cur_tid].recusion_depth<8);
	enum cmd_exec_result exec_ret=FINISHED;
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC_COARSE, &start);
	exec_ret=spec->proc(spec->module, cmd, &reply);
	clock_gettime(CLOCK_MONOTONIC_COARSE, &end);
	swarmkv_monitor_record_command(db->mod_monitor, spec->name, timespec_diff_usec(&start, &end));
	
	
	switch(exec_ret)
	{
		case FINISHED:
		{
			struct promise *p=future_to_promise(future_of_caller);
			promise_success(p, reply);
			swarmkv_reply_free(reply);		
			break;
		}
		case REDIRECT:
		{
			node_t peer;
			struct cmd_ctx *ctx=cmd_ctx_new(db, cmd, future_of_caller);
			node_init_from_reply(&peer, reply);
			swarmkv_reply_free(reply);
			//Should never redirect to myself
			assert(node_compare(&peer, &db->self));
			ctx->future_of_mine=future_create("peer_exec", peer_exec_on_success, generic_on_fail, ctx);
			__exec_cmd(db, &peer, cmd, ctx->future_of_mine);
			break;
		}
		case NEED_KEY_ROUTE:
		{
			struct swarmkv_cmd *keyspace_cmd=make_keyroute_cmd(spec->flag, cmd->argv[spec->key_offset], db->opts->dryrun? NULL:(&db->self));
			struct cmd_ctx *ctx=cmd_ctx_new(db, cmd, future_of_caller);
			ctx->future_of_mine=future_create("key_route", key_route_on_success, generic_on_fail, ctx);
			__exec_cmd(db, NULL, keyspace_cmd, ctx->future_of_mine);
			swarmkv_cmd_free(keyspace_cmd);
			keyspace_cmd=NULL;
			assert(reply==NULL);
			assert(spec->auto_route==1);
			break;
		}
		default:
		{
			assert(0);
			break;
		}
	}
	db->threads[cur_tid].recusion_depth--;
	return;
}

void command_register(struct swarmkv_cmd_spec **table, const char *name, const char *hint,
						int arity, int key_offset, enum cmd_key_flag flag, enum key_not_found_reply failover, int auto_route,
						command_proc_func *proc, struct swarmkv_module *module)
{
	struct swarmkv_cmd_spec *spec=NULL;

	spec=ALLOC(struct swarmkv_cmd_spec, 1);
	spec->name=name;
	spec->hint=hint;
	spec->arity=arity;
	spec->key_offset=key_offset;
	spec->flag=flag;
	spec->nokey_reply=failover;
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
					1, 1, CMD_KEY_RO, REPLY_NIL, AUTO_ROUTE,
					get_command, db->mod_store);
	command_register(&(db->command_table), "SET", "key value",
					2, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					set_command, db->mod_store);
	command_register(&(db->command_table), "INCRBY", "key increment",
					2, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					incrby_command, db->mod_store);
	command_register(&(db->command_table), "INCR", "key",
					1, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					incr_command, db->mod_store);
	command_register(&(db->command_table), "DECR", "key",
					1, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					decr_command, db->mod_store);
	/* Generic commands*/
					
	command_register(&(db->command_table), "DEL", "key",
					1, 1, CMD_KEY_RM, REPLY_INT_0, AUTO_ROUTE,
					del_command, db->mod_keyspace);
	command_register(&(db->command_table), "EXPIRE", "key seconds",
					2, 1, CMD_KEY_RW, REPLY_INT_0, AUTO_ROUTE,
					expire_command, db->mod_keyspace);
	command_register(&(db->command_table), "TTL", "key",
					1, 1, CMD_KEY_RO, REPLY_INT_MINORS1, AUTO_ROUTE,
					ttl_command, db->mod_keyspace);
	command_register(&(db->command_table), "PERSIST", "key",
					1, 1, CMD_KEY_RW, REPLY_INT_0, AUTO_ROUTE,
					persist_command, db->mod_keyspace);
	command_register(&(db->command_table), "TYPE", "key",
					1, 1, CMD_KEY_RO, REPLY_STR_NONE, AUTO_ROUTE,
					type_command, db->mod_store);
    command_register(&(db->command_table), "KEYSLOT", "key",
					1, 1, CMD_KEY_RO, REPLY_ERROR, AUTO_ROUTE,
					keyslot_command, db->mod_keyspace);

	/* Set commands */
	command_register(&(db->command_table), "SADD", "key member [member ...]",
					2, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					sadd_command, db->mod_store);
	command_register(&(db->command_table), "SREM", "key member [member ...]",
					2, 1, CMD_KEY_RW, REPLY_INT_0, AUTO_ROUTE,
					srem_command, db->mod_store);
	command_register(&(db->command_table), "SMEMBERS", "key",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					smembers_command, db->mod_store);
	command_register(&(db->command_table), "SISMEMBER", "key member",
					2, 1, CMD_KEY_RO, REPLY_INT_0, AUTO_ROUTE,
					sismember_command, db->mod_store);
	command_register(&(db->command_table), "SCARD", "key",
					1, 1, CMD_KEY_RO, REPLY_INT_0, AUTO_ROUTE,
					scard_command, db->mod_store);

	/* Hash commands */
	command_register(&(db->command_table), "HSET", "key field value [field value ...]",
					3, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					hset_command, db->mod_store);
	command_register(&(db->command_table), "HGET", "key field",
					2, 1, CMD_KEY_RO, REPLY_NIL, AUTO_ROUTE,
					hget_command, db->mod_store);
	command_register(&(db->command_table), "HMGET", "key field [field ...]",
					2, 1, CMD_KEY_RO, REPLY_NIL, AUTO_ROUTE,
					hmget_command, db->mod_store);
	command_register(&(db->command_table), "HDEL", "key field [field ...]",
					2, 1, CMD_KEY_RW, REPLY_INT_0, AUTO_ROUTE,
					hdel_command, db->mod_store);
	command_register(&(db->command_table), "HGETALL", "key",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					hgetall_command, db->mod_store);
	command_register(&(db->command_table), "HLEN", "key",
					1, 1, CMD_KEY_RO, REPLY_INT_0, AUTO_ROUTE,
					hlen_command, db->mod_store);
	command_register(&(db->command_table), "HKEYS", "key",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					hkeys_command, db->mod_store);
	command_register(&(db->command_table), "HINCRBY", "key field increment",
					3, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE,
					hincrby_command, db->mod_store);

	/* Token bucket commands */
	command_register(&(db->command_table), "TCFG", "key rate capacity",
					3, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE, 
					tcfg_command, db->mod_store);
	command_register(&(db->command_table), "TCONSUME", "key tokens [NORMAL|FORCE|FLEXIBLE]",
					2, 1, CMD_KEY_RW, REPLY_INT_MINORS1, AUTO_ROUTE,
					tconsume_command, db->mod_store);
	command_register(&(db->command_table), "TINFO", "key",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					tinfo_command, db->mod_store);
	command_register(&(db->command_table), "FTCFG", "key rate capacity divisor",
					4, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE, 
					ftcfg_command, db->mod_store);
	command_register(&(db->command_table), "FTCONSUME", "key member weight tokens",
					4, 1, CMD_KEY_RW, REPLY_INT_MINORS1, AUTO_ROUTE,
					ftconsume_command, db->mod_store);
	command_register(&(db->command_table), "FTINFO", "key",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					ftinfo_command, db->mod_store);
	command_register(&(db->command_table), "BTCFG", "key rate capacity number-of-buckets",
					4, 1, CMD_KEY_OW, REPLY_ERROR, AUTO_ROUTE, 
					btcfg_command, db->mod_store);
	command_register(&(db->command_table), "BTCONSUME", "key member tokens [NORMAL|FORCE|FLEXIBLE]",
					3, 1, CMD_KEY_RW, REPLY_INT_MINORS1, AUTO_ROUTE,
					btconsume_command, db->mod_store);
	command_register(&(db->command_table), "BTINFO", "key [member]",
					1, 1, CMD_KEY_RO, REPLY_EMPTY_ARRAY, AUTO_ROUTE,
					btinfo_command, db->mod_store);

	/* Debug Commands */
	command_register(&(db->command_table), "INFO", "[section]",
					0, KEY_OFFSET_NONE, CMD_KEY_NA, REPLY_NA, AUTO_ROUTE,
					info_command, &db->module);
	command_register(&(db->command_table), "DEBUG", "<subcommand>",
					1, KEY_OFFSET_NONE, CMD_KEY_NA, REPLY_NA, AUTO_ROUTE,
					debug_command, &db->module);
	command_register(&(db->command_table), "PING", "IP:port",
					1, KEY_OFFSET_NONE, CMD_KEY_NA, REPLY_NA, AUTO_ROUTE,
					ping_command, &db->module);
	command_register(&(db->command_table), "COMMAND LIST", "",
					0, KEY_OFFSET_NONE, CMD_KEY_NA, REPLY_NA, AUTO_ROUTE,
					command_list_command, &db->module);	

	command_register(&(db->command_table), "LATENCY", "<subcommand>",
					1, KEY_OFFSET_NONE, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					latency_command, db->mod_monitor);

	/* low-level state-based CRDT synchronization commands*/
	command_register(&(db->command_table), "CRDT ADD", "key [IP:port ...]",
					1, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_add_command, db->mod_store);
	command_register(&(db->command_table), "CRDT GET", "key",
					1, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_get_command, db->mod_store);
	command_register(&(db->command_table), "CRDT MERGE", "key blob [key blob ...]",
					2, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_merge_command, db->mod_store);
	command_register(&(db->command_table), "CRDT MEET", "key IP:port [IP:port ...]",
					2, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_meet_command, db->mod_store);
	command_register(&(db->command_table), "CRDT DEL", "key",
					1, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_del_command, db->mod_store);
    command_register(&(db->command_table), "CRDT KEYS", "tid pattern",
					1, KEY_OFFSET_TID, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_keys_command, db->mod_store);
    command_register(&(db->command_table), "CRDT EXISTS", "key",
					1, 2, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					crdt_exists_command, db->mod_store);
    command_register(&(db->command_table), "CRDT RLIST", "key",
					1, 2, CMD_KEY_NA, REPLY_NA,  NOT_AUTO_ROUTE,
					crdt_rlist_command, db->mod_store);
	command_register(&(db->command_table), "CRDT INFO", "key",
					1, 2, CMD_KEY_NA, REPLY_EMPTY_ARRAY, NOT_AUTO_ROUTE,
					crdt_info_command, db->mod_store);

	/* low-level keyspace operation commands */
	command_register(&(db->command_table), "KEYSPACE RLIST", "key",
					1, 2, CMD_KEY_RO, REPLY_NA, AUTO_ROUTE,
					keyspace_rlist_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE RADD", "key IP:port",
					1, 2, CMD_KEY_OW, REPLY_NA, AUTO_ROUTE,
					keyspace_radd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE XRADD", "key IP:port",
					1, 2, CMD_KEY_OW, REPLY_NA, AUTO_ROUTE,
					keyspace_xradd_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE KEYS", "tid pattern",//worker-thread-id
					1, KEY_OFFSET_TID, CMD_KEY_RO, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_keys_command, db->mod_keyspace);
    command_register(&(db->command_table), "KEYSPACE RDEL", "key IP:port",
					1, 2, CMD_KEY_RW, REPLY_NA, AUTO_ROUTE,
					keyspace_rdel_command, db->mod_keyspace); 
    command_register(&(db->command_table), "KEYSPACE EXISTS", "key",
					1, 2, CMD_KEY_RO, REPLY_NA, AUTO_ROUTE,
					keyspace_exists_command, db->mod_keyspace);

	/* low-level keyspace reorgnization commands */
	command_register(&(db->command_table), "KEYSPACE SETSLOT", "slot IMPORTING|MIGRATING|NODE|STABLE IP:port",
					2, KEY_OFFSET_SLOTID, CMD_KEY_NA, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_setslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE GETKEYSINSLOT", "slot",
					1, KEY_OFFSET_SLOTID, CMD_KEY_RO, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_getkeysinslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE ADDKEYSTOSLOT", "slot blob",
					2, KEY_OFFSET_SLOTID, CMD_KEY_OW, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_addkeystoslot_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE DELSLOTKEYS", "slot",
					1, KEY_OFFSET_SLOTID, CMD_KEY_RM, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_delslotkeys_command, db->mod_keyspace);
	command_register(&(db->command_table), "KEYSPACE COUNTKEYSINSLOT", "slot",
					1, KEY_OFFSET_SLOTID, CMD_KEY_RO, REPLY_NA, NOT_AUTO_ROUTE,
					keyspace_countkeysinslot_command, db->mod_keyspace);

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
char *swarmkv_get_command_hint(struct swarmkv *db, const char* cmd_name)
{
    int argc, buflen = strlen(cmd_name);
    sds *argv = sdssplitargs(cmd_name,&argc);
    int endspace = buflen && isspace(cmd_name[buflen-1]);

    if (argc == 0)
    {
        sdsfreesplitres(argv,argc);
        return NULL;
    }

    size_t i;
	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
        int argc_spec;
        sds *argv_spec = sdssplitargs(spec->name, &argc_spec);
        sds params=sdsdup(argv[0]);
        for(i=1; i<argc_spec && argc>=argc_spec; i++)
        {
            params = sdscatprintf(params, " %s", argv[i]);
        }
        sdsfreesplitres(argv_spec,argc_spec);

		if(0==strcasecmp(spec->name, params))
		{
            sds hint = sdsnew(spec->hint);
            int toremove = argc - argc_spec;
            while(toremove > 0 && sdslen(hint)) {
                if (hint[0] == '[') break;
                if (hint[0] == ' ') toremove--;
                sdsrange(hint,1,-1);
            }

            if (!endspace) {
                sds newhint = sdsnewlen(" ",1);
                newhint = sdscatsds(newhint,hint);
                sdsfree(hint);
                hint = newhint;
            }
            sdsfree(params);
            sdsfreesplitres(argv,argc);
			return hint;
		}
        sdsfree(params);
	}
    sdsfreesplitres(argv,argc);
	return NULL;
}
static void evloop_timeout_cb(evutil_socket_t fd, short event, void *arg)
{
    struct swarmkv_thread *thr = (struct swarmkv_thread *)arg;
    event_base_loopbreak(thr->evbase);
}
void swarmkv_caller_loop(struct swarmkv *db, int flags, struct timeval *tv)
{
	int tid=__gettid(db);
	//must initiate from caller threads, and caller thread ID is larger than worker thread ID
	assert(tid >= db->opts->nr_worker_threads);
	struct swarmkv_thread *thr=db->threads+tid;
	struct event *timeout_event = NULL;
	if(tv)
	{
		timeout_event = event_new(thr->evbase, -1, 0, evloop_timeout_cb, thr);
		evtimer_add(timeout_event, tv);
		event_base_loop(thr->evbase, flags);
		event_del(timeout_event);
		event_free(timeout_event);
	}
	else
	{
		event_base_loop(thr->evbase, flags);
	}
	
	return;
}
void swarmkv_caller_loop_break(struct swarmkv *db)
{
	int tid=__gettid(db);
	//must initiate from caller threads, and caller thread ID is larger than worker thread ID
	assert(tid >= db->opts->nr_worker_threads);
	event_base_loopbreak(db->ref_evbases[tid]);
}
long long swarmkv_caller_get_pending_commands(struct swarmkv *db)
{
	int tid=__gettid(db);
	//must initiate from caller threads, and caller thread ID is larger than worker thread ID
	assert(tid >= db->opts->nr_worker_threads);
	return swarmkv_rpc_mgr_count(db->rpc_mgr, tid);
}
struct swarmkv *swarmkv_open(struct swarmkv_options *opts, const char *db_name, char **err)
{
	struct swarmkv *db = NULL;
//	event_set_log_callback(libevent_log_cb);
	db=ALLOC(struct swarmkv, 1);
	strncpy(db->db_name, db_name, sizeof(db->db_name));
	opts->total_threads=opts->nr_caller_threads+opts->nr_worker_threads;
	db->threads=ALLOC(struct swarmkv_thread, opts->total_threads);
    /* adds locking, only required if accessed from separate threads */
	evthread_use_pthreads();

	strncpy(db->module.name, "db", sizeof(db->module.name));
	db->module.mod_ctx=db;

	db->opts=opts;
	if(opts->logger)
	{
		db->logger=opts->logger;
	}
	else
	{
		char log_path[1024]="";
        if(opts->logpath)
        {
            snprintf(log_path, sizeof(log_path), "%s/%s.log", opts->logpath, db_name);
        }
        else
        {
            snprintf(log_path, sizeof(log_path), "%s.log", db_name);
        }
        db->logger=log_handle_create(log_path, opts->loglevel);
	}


	uuid_copy(db->bin_uuid, opts->bin_uuid);
	if(opts->dryrun)
	{
	}
	db->ref_evbases=ALLOC(struct event_base*, opts->total_threads);
	struct event_config *ev_cfg=event_config_new();
	int ret=event_config_set_max_dispatch_interval(ev_cfg, opts->eb_max_interval, opts->eb_max_callbacks, 0);
	assert(ret==0);
	for(int i=0; i<opts->total_threads; i++)
	{
		if(i>opts->nr_worker_threads)
		{
			db->threads[i].evbase=event_base_new_with_config(ev_cfg);
			event_base_priority_init(db->threads[i].evbase, 2);
		}
		else
		{
			db->threads[i].evbase=event_base_new();
		}
		db->threads[i].db=db;
		db->ref_evbases[i]=db->threads[i].evbase;
	}
	event_config_free(ev_cfg);

	db->mod_monitor=swarmkv_monitor_new(db->opts);

	db->rpc_mgr=swarmkv_rpc_mgr_new(db->ref_evbases, opts->total_threads, opts->cluster_timeout_us);
	db->mesh=swarmkv_mesh_new(db->ref_evbases, opts->total_threads, db->logger);
	swarmkv_mesh_set_on_msg_cb(db->mesh, __on_msg_callback, db);
	

	//Note: if the cluster_port is 0, swarmkv_net_new updates db->self.cluster_port.
	db->net=swarmkv_net_new(db->ref_evbases, db->opts->nr_worker_threads, opts, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_net_set_on_msg_callback(db->net, __on_msg_callback, db);
	swarmkv_net_set_monitor_handle(db->net, db->mod_monitor);

	node_init(&db->self, opts->cluster_announce_ip, opts->cluster_announce_port);
	

	db->mod_keyspace=swarmkv_keyspace_new(db->ref_evbases, db->opts->nr_worker_threads, db->opts, db_name, db->logger, err);
	if(*err)
	{
		goto error_out;
	}
	swarmkv_keyspace_set_exec_cmd_handle(db->mod_keyspace, db);
	swarmkv_keyspace_set_monitor_handle(db->mod_keyspace, db->mod_monitor);

	db->mod_store=swarmkv_store_new(db->opts);
	swarmkv_store_set_exec_cmd_handle(db->mod_store, db);
	swarmkv_store_set_monitor_handle(db->mod_store, db->mod_monitor);


	command_spec_init(db);

	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		swarmkv_monitor_register_command(db->mod_monitor, spec->name);
	}
	swarmkv_monitor_register_event(db->mod_monitor, MONITOR_INTER_THREAD_RPC);
	swarmkv_threads_run(db);
	
	swarmkv_keyspace_start(db->mod_keyspace);

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
	swarmkv_options_free(db->opts);
	db->opts=NULL;
	free(db);
	db=NULL;
	return NULL;
}
void swarmkv_close(struct swarmkv * db)
{
	for(size_t i=0; i<db->opts->total_threads; i++)
	{
		event_base_loopexit(db->threads[i].evbase, NULL);		
		db->threads[i].is_dispatching=0;
	}
	for(size_t i=0; i<db->opts->nr_worker_threads; i++)
	{
		pthread_join(db->threads[i].thr, NULL);
	}

	swarmkv_rpc_mgr_free(db->rpc_mgr);
	db->rpc_mgr=NULL;
	swarmkv_monitor_free(db->mod_monitor);
	db->mod_monitor=NULL;
	swarmkv_store_free(db->mod_store);
	db->mod_store=NULL;
	swarmkv_keyspace_free(db->mod_keyspace);
	db->mod_keyspace=NULL;
	swarmkv_mesh_free(db->mesh);
	db->mesh=NULL;
	swarmkv_net_free(db->net);
	db->net=NULL;


	struct swarmkv_cmd_spec *spec=NULL, *tmp_spec=NULL;
	
	HASH_ITER(hh, db->command_table, spec, tmp_spec)
	{
		HASH_DEL(db->command_table, spec);
		free(spec);
	}
	char str_uuid[37]="";
	uuid_unparse(db->opts->bin_uuid, str_uuid);
	log_info(db->logger, MODULE_SWAMRKV_CORE, "swarmkv instance %s of cluster %s is closed.", str_uuid, db->db_name);

	if(db->opts->logger != db->logger)
	{
		log_handle_destroy(db->logger);
		db->logger=NULL;
	}

	for(size_t i=0; i<db->opts->total_threads; i++)
	{
		event_base_free(db->threads[i].evbase);
	}
	free(db->threads);
	db->threads=NULL;
	free(db->ref_evbases);
	db->ref_evbases=NULL;

	db->opts->is_assigned_to_db=0;
	swarmkv_options_free(db->opts);
	db->opts=NULL;
	pthread_barrier_destroy(&db->barrier);
	free(db);
	return;
}
const char *swarmkv_self_address(const struct swarmkv *db)
{
	return db->self.addr;
}
