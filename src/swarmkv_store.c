#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_net.h"
#include "swarmkv_keyspace.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"

#include "sds.h"

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/prctl.h>


#define	MODULE_SWAMRKV_STORE	"swarmkv.store"

int SWARMKV_VERSION_2_0_0_tiger_year=0;

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
};
struct swarmkv_thread_ctx
{
	pthread_t thr;
	int readable_tid;
	unsigned int thread_id;
	struct swarmkv_store *store;

	
	time_t lastime;
};

struct swarmkv_store
{
	struct swarmkv_params para;
	int nr_worker_threads;
	struct swarmkv_thread_ctx * worker_threads[SWARMKV_THREAD_MAX];
	
	int my_node_id;
	const char* my_addr;
	UT_array* cluster_nodes;
	struct key_route_entry* hash_route_table;
	
	struct kv_container *hash_own_kv;
	struct kv_container *hash_cached_kv;
	
	struct swarmkv_keyspace* ks;
	struct swarmkv_net* net;
	pthread_rwlock_t rwlock;
	void* logger;
};

int swarmkv_store_lookup_key_owner(struct swarmkv_store *store, const char *key, size_t keylen, struct swarmkv_node* owner)
{
	return 0;
}


void swarmkv_store_create_thread_ctx(struct swarmkv_store *store)
{
	int i=0;
	for(i=0; i<store->nr_worker_threads; i++)
	{
		store->worker_threads[i] = ALLOC(struct swarmkv_thread_ctx, 1);
		store->worker_threads[i]->thread_id = i;
		store->worker_threads[i]->store = store;		
	}
}
void *swarmkv_worker_thread(void *arg)
{
	struct swarmkv_thread_ctx* ctx = (struct swarmkv_thread_ctx *) arg;
	ctx->readable_tid = syscall(SYS_gettid);
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv-%u", ctx->thread_id);
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);

	swarmkv_net_dispath(ctx->store->net, ctx->thread_id);
	return NULL;
}

void swarmkv_threads_run(struct swarmkv_store * store)
{
	int i = 0;
	for (i = 0; i < store->nr_worker_threads; i++)
	{
		pthread_create(&store->worker_threads[i]->thr, NULL, swarmkv_worker_thread, (void *) (store->worker_threads[i]));
	}
	return;
}

struct swarmkv_store *swarmkv_open(const char * cluster_name, unsigned int http_port, unsigned int p2p_port, void * logger)
{
	struct swarmkv_store *store = NULL;
	int ret = 0;

	store=ALLOC(struct swarmkv_store, 1);
	store->logger=logger;
	store->nr_worker_threads=2;
	store->para.http_port=http_port;
	store->para.p2p_port=p2p_port;
	swarmkv_store_create_thread_ctx(store);

	store->net=swarmkv_net_new(p2p_port, store->nr_worker_threads, store->logger);
	struct event_base *evbase=swarmkv_net_get_eventbase(store->net, 0);
	store->ks=swarmkv_keyspace_new(cluster_name, evbase, http_port, p2p_port, logger);
	if(store->para.dry_run)
	{
		swarmkv_keyspace_set_dryrun(store->ks);
	}
	ret=swarmkv_keyspace_init(store->ks);
	if(ret<0)
	{
		goto error_out;
	}
	swarmkv_threads_run(store);
	
	return store;
error_out:
	if(store->net)
	{
		swarmkv_net_free(store->net);
		store->net=NULL;
	}
	if(store->ks)
	{
		swarmkv_keyspace_free(store->ks);
		store->ks=NULL;
	}
	free(store);
	store=NULL;
	return NULL;
}
void swarmkv_close(struct swarmkv_store * store)
{
	swarmkv_keyspace_free(store->ks);
	store->ks=NULL;
	swarmkv_net_free(store->net);
	//todo
	free(store);
	return;
}

int swarmkv_store_value_owner_write(struct kv_container *hash_own_kv, enum swarmkv_command op, const char *key, size_t keylen, 
					enum swarmkv_data_type data_type, const void *value, size_t vallen)
{	
	struct kv_container* kvc=NULL;
	HASH_FIND(hh, hash_own_kv, key, keylen, kvc);
	if(!kvc)
	{
		
	}
	return 0;
}
const struct kv_pair* value_owner_read(struct kv_container *hash_own_kv, const char *key, size_t keylen)
{
	struct kv_container* kvc=NULL;
	HASH_FIND(hh, hash_own_kv, key, keylen, kvc);
	if(kvc)
	{
		return &(kvc->head);
	}
	else
	{
		return NULL;
	}
}
int value_owner_is_key_exisits(struct kv_container *hash_own_kv, const char *key, size_t keylen)
{
	return 0;
}

int lookup_value_owner(struct key_route_entry* hash_route_table, const char *key, size_t keylen)
{
	return 0;
}
void swarmkv_set_string(struct swarmkv_store *store,
				const char *key, size_t keylen, const char *value, size_t vallen)
{
	int ret=0;
	ret=value_owner_is_key_exisits(store->hash_own_kv, key, keylen);
	sds msg_buff=NULL;
	struct swarmkv_node key_owner;
	if(ret)//local value
	{
		swarmkv_store_value_owner_write(store->hash_own_kv, SWARMKV_CMD_SET, key, keylen, SWARMKV_DATA_TYPE_STRING, value, vallen);
	}
	else
	{
		ret=swarmkv_store_lookup_key_owner(store, key, keylen, &key_owner);
//		key_owner=utarray_eltptr(store->cluster_nodes, node_id);
//		swarmkv_msg_build_set_string(key, keylen, value, vallen, &msg_buff, &msglen);
		swarmkv_net_send(store->net, NULL, msg_buff, sdslen(msg_buff));
		sdsfree(msg_buff);
	}
}

void swarmkv_get_string(struct swarmkv_store *store,
				const char *key, size_t keylen, swarmkv_string_type_callback_func_t *cb, void *arg)
{
	int ret=0;
	const struct kv_pair* kv=NULL;	
	struct swarmkv_node key_owner;
	ret=value_owner_is_key_exisits(store->hash_own_kv, key, keylen);
	if(ret)//local value
	{
		kv=value_owner_read(store->hash_own_kv, key, keylen);
		if(kv)
		{
			cb(key, keylen, kv->value, kv->vallen, arg);
		}
	}
	else
	{
		swarmkv_store_lookup_key_owner(store, key, keylen, &key_owner);
		//send_cmd_to_key_owner(key_owner_addr, SWARMKV_CMD_GET, key, keylen, SWARMKV_DATA_TYPE_STRING, NULL, 0);
	}
	return;
}
int swarmkv_hset_string(struct      swarmkv_store *store, const struct swarmkv_writeoptions *options,
				const char *key, size_t keylen, const char* member, size_t memberlen, const char *value, size_t vallen)
{
	return 0;
}

