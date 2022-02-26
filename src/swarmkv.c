#include "swarmkv/swarmkv.h"

#include "swarmkv_common.h"
#include "swarmkv_limits.h"
#include "swarmkv_utils.h"

#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_message.h"
#include "swarmkv_net.h"

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

enum swarmkv_txn_state
{
	STATE_WAIT_KEY_OWNER, //wait for key owner to reply object onwer information
	STATE_WAIT_OBJ_ONWER //wait for object owner to execute the command
};
struct swarmkv_transaction
{
	unsigned long long sequence;
	struct timespec create_time;
	enum swarmkv_txn_state state;
	struct swarmkv_cmd* cmd;
	swarmkv_cmd_callback_func_t* callback_func;
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
	struct swarmkv_thread_ctx * worker_threads[SWARMKV_THREAD_MAX];
	struct swarmkv_keyspace* ks;
	struct swarmkv_transaction* transaction_table;
	struct swarmkv_store* store;
	struct swarmkv_net* net;
	pthread_rwlock_t rwlock;
	void* logger;
	unsigned long long txn_sequence_generator;

};
struct swarmkv_readoptions
{

};
struct swarmkv_writeoptions
{

};


static struct swarmkv_transaction* swarmkv_transaction_new(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	struct swarmkv_transaction *txn=ALLOC(struct swarmkv_transaction, 1);
	txn->sequence=db->txn_sequence_generator++;
	txn->callback_func=cb;
	txn->callback_arg=cb_arg;
	txn->cmd=swarmkv_cmd_dup(cmd);
	HASH_ADD(hh, db->transaction_table, sequence, sizeof(txn->sequence), txn);
	return txn;
}
static void swarmkv_transaction_free(struct swarmkv *db, struct swarmkv_transaction *txn)
{
	swarmkv_cmd_free(txn->cmd);
	HASH_DELETE(hh, db->transaction_table, txn);
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
static void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	int self_is_keyowner=0, self_is_objowner=0, is_cache_hit=0;
	struct swarmkv_node keyowner, objowner; 
	struct swarmkv_cmd *cmd_get_obj_owner=NULL;
	struct swarmkv_reply *reply=NULL;

	const char* key=cmd->args[0];
	size_t keylen=sdslen(cmd->args[0]);

	self_is_keyowner=swarmkv_keyspace_get_key_owner(db->ks, key, keylen, &keyowner);
	if(self_is_keyowner)
	{
		self_is_objowner=swarmkv_keyspace_set_obj_owner_if_NoneXist(db->ks, key, keylen, &(db->self), &objowner);
	}
	if(self_is_objowner)
	{
		swarmkv_store_exec(db->store, cmd, &reply);
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
	struct swarmkv_transaction* txn=swarmkv_transaction_new(db, cmd, cb, cb_arg);
	sds buff=NULL;
	struct swarmkv_msg* msg=NULL;
	msg=swarmkv_msg_new(db->bin_uuid, txn->sequence);
	struct swarmkv_node* dest=NULL;
	if(!self_is_keyowner)
	{
		txn->state=STATE_WAIT_KEY_OWNER;
		cmd_get_obj_owner=swarmkv_cmd_new(SWARMKV_OP_CLUSTER_OBJ_OWNER, 2);
		cmd_get_obj_owner->args[0]=sdsnewlen(key, keylen);
		cmd_get_obj_owner->args[1]=sdsnew("NX");//"SET if Not eXists".
		swarmkv_msg_add_cmd(msg, cmd_get_obj_owner);
		cmd_get_obj_owner=NULL;
		dest=&keyowner;
	}
	else if(!self_is_objowner)
	{
		txn->state=STATE_WAIT_OBJ_ONWER;
		struct swarmkv_cmd* _cmd=swarmkv_cmd_dup(cmd);
		swarmkv_msg_add_cmd(msg, _cmd);
		_cmd=NULL;
		dest=&objowner;

	}
	else
	{
		assert(0);
	}
	buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(db->net, dest, buff);
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;

fast_out:
	if(cmd_get_obj_owner)
	{
		swarmkv_cmd_free(cmd_get_obj_owner);
	}
	if(reply)
	{
		swarmkv_reply_free(reply);
	}
	return;

}

void msg_read_callback(const struct swarmkv_node * peer, const char* data, size_t len, void* arg)
{
	struct swarmkv* db=(struct swarmkv*)arg;
	
	struct swarmkv_reply* reply=NULL;
	enum swarmkv_msg_type msg_type;
	uuid_t msg_uuid;
	struct swarmkv_msg *msg_received=NULL, *msg_to_send=NULL;
	struct swarmkv_node obj_owner;
	unsigned long long msg_call_sequence=0;
	int got_obj_owner=0;
	msg_received=swarmkv_msg_deserialize(data, len);
	msg_type=swarmkv_msg_get_type(msg_received);
	msg_call_sequence=swarmkv_msg_read_transaction_sequence(msg_received);
	if(msg_type==SWARMKV_MSG_TYPE_CMD)
	{
		struct swarmkv_cmd* cmd=NULL;
		cmd=swarmkv_msg_get_cmd(msg_received);
		switch(cmd->op)
		{
			case SWARMKV_OP_CLUSTER_OBJ_OWNER:
				if(cmd->n_args==2 && 0==strncmp(cmd->args[1], "NX", 2))
				{
					swarmkv_keyspace_set_obj_owner_if_NoneXist(db->ks, cmd->args[0], sdslen(cmd->args[0]), peer, &obj_owner);
					got_obj_owner=1;
				}
				else
				{
					got_obj_owner=swarmkv_keyspace_get_obj_owner(db->ks, cmd->args[0], sdslen(cmd->args[0]), &obj_owner);
				}

				if(got_obj_owner)
				{
					reply=ALLOC(struct swarmkv_reply, 1);
					reply->type=SWARMKV_REPLY_ARRAY;
					reply->n_element=2;
					reply->element=ALLOC(struct swarmkv_reply*, reply->n_element);
					reply->element[0]=ALLOC(struct swarmkv_reply, 1);
					reply->element[0]->type=SWARMKV_REPLY_STRING;
					reply->element[0]->len=strlen(obj_owner.ip_addr)+1;
					reply->element[0]->str=strdup(obj_owner.ip_addr);
					
					reply->element[1]=ALLOC(struct swarmkv_reply, 1);
					reply->element[1]->type=SWARMKV_REPLY_INTEGER;
					reply->element[1]->integer=obj_owner.p2p_port;
				}
				else
				{
					reply=ALLOC(struct swarmkv_reply, 1);
					reply->type=SWARMKV_REPLY_NIL;
				}
				break;
			default:
				swarmkv_store_exec(db->store, cmd, &reply);
				break;
		}
		swarmkv_msg_read_txn_initiator_uuid(msg_received, msg_uuid);
		msg_to_send=swarmkv_msg_new(msg_uuid, msg_call_sequence);
		swarmkv_msg_add_reply(msg_to_send, reply);
		reply=NULL;
		sds buff=swarmkv_msg_serialize(msg_to_send);
		swarmkv_net_send(db->net, peer, buff);
		swarmkv_msg_free(msg_to_send);
		msg_to_send=NULL;
		sdsfree(buff);
		buff=NULL;
		//cache sync
		//msg_to_send=swarmkv_msg_new(const char * uuid, unsigned long long call_sequence)
	}
	else
	{
		struct swarmkv_reply* reply=NULL;
		struct swarmkv_transaction* txn=NULL;
		reply=swarmkv_msg_get_reply(msg_received);
		msg_call_sequence=swarmkv_msg_read_transaction_sequence(msg_received);
		HASH_FIND(hh, db->transaction_table, &msg_call_sequence, sizeof(msg_call_sequence), txn);
		if(txn)
		{
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
						|| reply->element[0]->type!=SWARMKV_REPLY_STRING
						|| reply->element[1]->type!=SWARMKV_REPLY_INTEGER)
					{
						MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Invalid reply for CLUSTER OBJOWNER");
					}
					memset(&obj_owner, 0, sizeof(obj_owner));
					strncpy(obj_owner.ip_addr, reply->element[0]->str, sizeof(obj_owner.ip_addr));
					obj_owner.p2p_port=reply->element[1]->integer;
					msg_to_send=swarmkv_msg_new(db->bin_uuid, txn->sequence);
					swarmkv_msg_add_cmd(msg_to_send, txn->cmd);
					txn->cmd=NULL;
					txn->state=STATE_WAIT_OBJ_ONWER;
					sds buff=NULL;
					buff=swarmkv_msg_serialize(msg_to_send);
					txn->cmd=swarmkv_msg_get_cmd(msg_to_send);
					swarmkv_net_send(db->net, &obj_owner, buff);
					swarmkv_msg_free(msg_to_send);
					msg_to_send=NULL;
					sdsfree(buff);
					buff=NULL;
				}
			}
			else
			{
				txn->callback_func(reply, txn->callback_arg);
				swarmkv_transaction_free(db, txn);
			}
		}
		else
		{
			MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Unknown call sequence %llu", msg_call_sequence);
		}
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
	
	db->txn_sequence_generator=1;
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
	

	struct swarmkv_transaction *txn=NULL, *tmp_txn=NULL;
	
	HASH_ITER(hh, db->transaction_table, txn, tmp_txn)
	{
		swarmkv_transaction_free(db, txn);
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
	return;
}


