#include "swarmkv.h"
#include "swarmkv_store.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_message.h"
#include "swarmkv_net.h"

int SWARMKV_VERSION_2_0_0_tiger_year=0;
#define	MODULE_SWAMRKV_CORE	"swarmkv.core"

struct swarmkv_params
{
	char* self_ip_str;
	unsigned int http_port;
	unsigned int p2p_port;
	size_t n_bootstrap_nodes;
	char bootstrap_node_addrs[MAX_BOOTSTRAP_NODES][MAX_IPv6_ADDR_STR_LEN];
	uint16_t boostrap_node_port_nr[MAX_BOOTSTRAP_NODES];	//network order
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

struct key_route_entry
{
	char* key;
	size_t keylen;
	int owner_node_id;
	UT_hash_handle hh;
};
struct key_route_table
{
	struct key_route_entry* hash_route_table;
	pthread_rwlock_t rwlock;
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
	swarmkv_cmd_callback_func_t callback_func;
	void* callback_arg;
	UT_hash_handle hh;
};
struct swarmkv
{
	struct swarmkv_params para;
	int nr_worker_threads;
	char uuid[37];
	struct swarmkv_node self;
	struct swarmkv_thread_ctx * worker_threads[SWARMKV_THREAD_MAX];
	struct swarmkv_keyspace* ks;
	struct key_route_table* key_route;	
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
static struct swarmkv_transaction* swarmkv_transaction_new(struct swarmkv *db, struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
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

	HASH_DELETE(hh, db->transaction_table, txn);
	free(txn);
}

//return 1if 
int obj_route_lookup_obj_owner(struct key_route_table *key_route, const char *key, size_t keylen, struct swarmkv_node* owner)
{
	return 0;
}
int obj_route_add_obj_owner(struct key_route_table *key_route, const char* key, size_t keylen, struct swarmkv_node* owner)
{
}

void swarmkv_create_thread_ctx(struct swarmkv *db)
{
	int i=0;
	for(i=0; i<db->nr_worker_threads; i++)
	{
		db->worker_threads[i] = ALLOC(struct swarmkv_thread_ctx, 1);
		db->worker_threads[i]->thread_id = i;
		db->worker_threads[i]->store = db;		
	}
}
static void peer_conn_bev_readcb(struct bufferevent * bev, void * arg)
{
	struct swarmkv *db=(struct swarmkv*) arg;	
	struct evbuffer * input_buffer = bufferevent_get_input(bev);
	swarmkv_msg_read(struct evbuffer * buff, reply or cmd?)
}
void msg_read_callback(sds msg_buff, void* arg)
{
	struct swarmkv* db=(struct swarmkv*)arg;
	
	struct swarmkv_reply* reply=NULL;
	enum swarmkv_msg_type msg_type=0;
	struct swarmkv_msg *msg_received=NULL, *msg_to_send=NULL;
	struct swarmkv_node obj_owner, *p=NULL, requester=NULL;
	unsigned long long msg_call_sequence=0;
	int ret=0;
	msg_received=swarmkv_msg_deserialize(msg_buff);
	msg_type=swarmkv_msg_get_type(msg_received);
	msg_call_sequence=swarmkv_msg_read_transaction_sequence(msg_received);
	swarmkv_msg_read_node(msg_received, &requester);
	if(msg_type==SWARMKV_MSG_TYPE_CMD)
	{
		struct swarmkv_cmd* cmd=NULL;
		cmd=swarmkv_msg_get_reply(msg_received);
		switch(cmd->op)
		{
			case SWARMKV_OP_CLUSTER_OBJ_OWNER:
				ret=obj_route_lookup_obj_owner(db->key_route, cmd->args[0], sdslen(cmd->args[0]), &obj_owner);
				if(ret==1)
				{
					p=&obj_owner;
				}
				else if(cmd->n_args==2 && 0==strncmp(cmd->args[1], "NX", 2))
				{
					p=requester;
					obj_route_add_obj_owner(db->key_route, cmd->args[0], sdslen(cmd->args[0]), p);
				}
				
				if(p)
				{
					reply=ALLOC(struct swarmkv_reply, 1);
					reply->type=SWARMKV_REPLY_ARRAY;
					reply->n_element=2;
					reply->element[0]=ALLOC(struct swarmkv_reply, 1);
					reply->element[0]->type=SWARMKV_REPLY_STRING;
					reply->element[0]->str=strdup(p->ip_addr);
					
					reply->element[1]=ALLOC(struct swarmkv_reply, 1);
					reply->element[1]->type=SWARMKV_REPLY_INTEGER;
					reply->element[1]->integer=p->p2p_port;
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
		msg_to_send=swarmkv_msg_new(msg_received->txn_initiator, msg_call_sequence);
		swarmkv_msg_add_reply(msg_to_send, reply);
		reply=NULL;
		sds buff=swarmkv_msg_serialize(msg_to_send);
		swarmkv_net_send(db->net, requester, buff);
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
		struct swarmkv_transaction* cmd_ctx=NULL;
		reply=swarmkv_msg_get_reply(msg_received);
		msg_call_sequence=swarmkv_msg_read_transaction_sequence(msg_received);
		HASH_FIND(hh, db->transaction_table, &msg_call_sequence, sizeof(msg_call_sequence), cmd_ctx);
		if(cmd_ctx)
		{
			if(cmd_ctx->state==STATE_WAIT_KEY_OWNER)
			{
				if(reply->type!=SWARMKV_REPLY_NIL)
				{
					cmd_ctx->callback_func(reply, cmd_ctx->callback_arg);
					HASH_DELETE(hh, db->transaction_table, cmd_ctx);
					swarmkv_cmd_free(cmd_ctx->cmd);
					cmd_ctx->cmd=NULL;
					free(cmd_ctx);
				}
				else
				{
					if(reply->type!=SWARMKV_REPLY_ARRAY || reply->n_element!=2
						|| reply->element[0].type!=SWARMKV_REPLY_STRING
						|| reply->element[1].type!=SWARMKV_REPLY_INTEGER)
					{
						MESA_handle_runtime_log(db->logger, RLOG_LV_FATAL, MODULE_SWAMRKV_CORE, "Invalid reply for CLUSTER OBJOWNER");
					}
					obj_owner.ip_addr=strdup(reply->element[0]->str);
					obj_owner.p2p_port=reply->element[1].integer;
					msg_to_send=swarmkv_msg_new(db->uuid, cmd_ctx->sequence);
					swarmkv_msg_add_cmd(msg_to_send, cmd_ctx->cmd);
					cmd_ctx->cmd=NULL;
					cmd_ctx->state=STATE_WAIT_OBJ_ONWER;
					sds buff=NULL;
					buff=swarmkv_msg_serialize(msg_to_send);
					cmd_ctx->cmd=swarmkv_msg_get_cmd(msg_to_send);
					swarmkv_net_send(db->net, &obj_owner, buff);
					swarmkv_msg_free(msg_to_send);
					msg_to_send=NULL;
					sdsfree(buff);
					buff=NULL;
				}
			}
			else
			{
				cmd_ctx->callback_func(reply, cmd_ctx->callback_arg);
				swarmkv_cmd_free(cmd_ctx->cmd);
				cmd_ctx->cmd=NULL;
				free(cmd_ctx);
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

struct swarmkv_store *swarmkv_open(const char * cluster_name, unsigned int http_port, unsigned int p2p_port, void * logger)
{
	struct swarmkv *db = NULL;
	int ret = 0;

	db=ALLOC(struct swarmkv, 1);
	db->logger=logger;
	db->nr_worker_threads=2;
	db->para.http_port=http_port;
	db->para.p2p_port=p2p_port;
	swarmkv_create_thread_ctx(db);

	srand(time());
	db->txn_sequence_generator=rand();
	db->net=swarmkv_net_new(p2p_port, db->nr_worker_threads, db->logger);
	struct event_base *evbase=swarmkv_net_get_eventbase(db->net, 0);
	db->ks=swarmkv_keyspace_new(cluster_name, evbase, http_port, p2p_port, logger);
	if(db->para.dry_run)
	{
		swarmkv_keyspace_set_dryrun(db->ks);
	}
	ret=swarmkv_keyspace_init(db->ks);
	if(ret<0)
	{
		goto error_out;
	}
	swarmkv_threads_run(db);
	
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
	swarmkv_keyspace_free(db->ks);
	db->ks=NULL;
	swarmkv_net_free(db->net);
	//todo
	free(db);
	return;
}
static void swarmkv_exec(struct swarmkv *db, const struct swarmkv_cmd *cmd, swarmkv_cmd_callback_func_t * cb, void * cb_arg)
{
	int ret=0, is_keyower=0, is_objowner=0, is_cache_hit=0;
	struct swarmkv_node keyowner, objowner; 
	struct swarmkv_cmd *cmd_get_obj_owner=NULL;
	struct swarmkv_reply *reply=NULL;
	sds msg_for_local_cmd=NULL;

	const char* key=cmd->args[0];
	size_t keylen=sdslen(cmd->args[0]);
	
	is_keyower=swarmkv_keyspace_get_keyowner(db->ks, key, keylen, &keyowner);
	if(is_keyower)
	{
		is_objowner=obj_route_lookup_obj_owner(db->key_route, key, keylen, &objowner);
		if(ret==1 && 0==memcmp(objowner, &db->self, sizeof(*objowner)))
		{
			is_objowner=1;
		}
	}
	if(is_objowner)
	{
		swarmkv_store_exec(db->store, cmd, &reply);
		cb(reply, cb_arg);
		goto fast_out;
	}
	if(is_read_operator(cmd->op) && !db->para.disable_cache)
	{
		is_cache_hit=store_cache_cmd_exec(cmd_get_obj_owner, reply);
		if(is_cache_hit)
		{
			cb(reply, cb_arg);
			goto fast_out;
		}
	}
	struct swarmkv_transaction* txn=swarmkv_transaction_new(db, cmd, cb, cb_arg);
	
	cmd_get_obj_owner=swarmkv_cmd_new(SWARMKV_OP_CLUSTER_OBJ_OWNER, 2);
	cmd_get_obj_owner->args[0]=sdsnewlen(key, keylen);
	cmd_get_obj_owner->args[1]=sdsnew("NX");//"SET if Not eXists".
	sds buff=NULL;
	struct swarmkv_msg* msg=NULL;
	msg=swarmkv_msg_new(db->uuid, txn->sequence);
	swarmkv_msg_set_node(msg, &db->self);
	swarmkv_msg_add_cmd(msg, cmd_get_obj_owner);
	cmd_get_obj_owner=NULL;
	buff=swarmkv_msg_serialize(msg);
	swarmkv_net_send(db->net, &keyowner, buff);
	swarmkv_msg_free(msg);
	msg=NULL;
	sdsfree(buff);
	buff=NULL;
	
fast_out:
	swarmkv_cmd_free(cmd_get_obj_owner);
	swarmkv_reply_free(reply);
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
int swarmkv_hset_string(struct      swarmkv_store *store, const struct swarmkv_writeoptions *options,
				const char *key, size_t keylen, const char* member, size_t memberlen, const char *value, size_t vallen)
{
	return 0;
}
struct swarmkv_cmd* swarmkv_cmd_new(enum swarmkv_operator op, size_t n_args)
{
	struct swarmkv_cmd* cmd=ALLOC(struct swarmkv_cmd, 1);
	cmd->op=op;
	cmd->n_args=n_args;
	assert(cmd->n_args<MAX_CMD_ARGS);
	return cmd;
}
void swarmkv_cmd_free(struct swarmkv_cmd* p)
{
	size_t i=0;
	for(i=0; i<p->n_args; i++)
	{
		sdsfree(p->args[i]);
		p->args[i]=NULL;
	}
	free(p);
}
struct swarmkv_cmd* swarmkv_cmd_dup(struct swarmkv_cmd* origin)
{
	struct swarmkv_cmd* copy=ALLOC(struct swarmkv_cmd, 1);
	copy->op=origin->op;
	copy->n_args=origin->n_args;
	size_t i=0;
	for(i=0; i<origin->n_args; i++)
	{
		copy->args[i]=sdsdup(origin->args[i]);
	}
	return copy;
}

