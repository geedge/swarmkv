#include "swarmkv/swarmkv.h"
#include "swarmkv_store.h"
#include "swarmkv_utils.h"
#include "uthash/uthash.h"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
int SWARMKV_VERSION_2_0_0_20220124=0;
int swarmkv_store_lookup_key_owner(struct swarmkv_store *store, const char *key, size_t keylen)
{
	int slot_id = crc32(key, keylen)%SLOT_NUM;
	return store->slots[slot_id].key_owner_node_id;
}


void *swarmkv_work_thread(void *arg)
{
	struct swarmkv_thread_ctx* ctx = (struct swarmkv_thread_ctx *) arg;
	ctx->readable_tid = syscall(SYS_gettid);
	char thread_name[16];
	snprintf(thread_name, sizeof(thread_name), "swarmkv:worker-%d", ctx->thread_id);
	prctl(PR_SET_NAME, (unsigned long long) thread_name, NULL, NULL, NULL);

	event_base_dispatch(ctx->evbase);

	return NULL;
}
void *swarmkv_main_thread(void *arg)
{

}

static int swarmkv_parse_bootstrap_info(struct swarmkv_params *para, const char *bootstraps) {
	if (bootstraps == NULL || strlen(bootstraps) == 0) {
		printf(
			"Please set your ip&port and bootstrap_nodes ip&port. Format: "
			"\n\tproto=udp;self=ip:port;peers=ip0:port0,ip1:port1,... or proto=rdma;self=ip;peers=ip0,ip1,... \n");
		return -1;
	}
	// bootstraps example: "proto=udp;self=0.0.0.0:8323;peers=192.168.0.100:8323,192.168.0.101:8323"
	uint64_t bs_info_len = strlen(bootstraps);
	char *bootstrap_info_copy = ALLOC(char, bs_info_len + 1);
	strcpy(bootstrap_info_copy, bootstraps);

	int meet_node_cnt = 0;


	char *proto = NULL; //proto=udp, proto=rdma.
	char *my_addr = NULL;	//Address used to bind with. e.g. self=192.168.1.1:8323

	char *self_ip = NULL;
	char *next_ptr = NULL;
	char *tmp=NULL;

	//get proto string
	proto = strtok_r(bootstrap_info_copy, ";", &next_ptr);

	my_addr = strtok_r(NULL, ";", &next_ptr);
	strtok_r(my_addr, "=", &my_addr);

	if (strcpy(proto, "proto=udp"))
	{
		char *listen_port = NULL;
		self_ip = strtok_r(my_addr, ":", &listen_port);
		uint64_t ip_len = strlen(self_ip);
		para->self_ip_str= ALLOC(char, ip_len + 1);
		memcpy(para->self_ip_str, self_ip, ip_len);
		para->listen_port=atoi(listen_port);
		
		char *pp = strtok_r(NULL, ";", &next_ptr);
		if (pp != NULL) {
			char *peer_node_addr = NULL;
			strtok_r(pp, "=", &peer_node_addr);

			peer_node_addr = strtok_r(peer_node_addr, ",", &next_ptr);
			while (peer_node_addr != NULL && para->n_bootstrap_nodes< MAX_BOOTSTRAP_NODES)
			{	
				char *peer_port_str = NULL;
				char *peer_ip = NULL;
				uint16_t peer_port;
				peer_ip = strtok_r(peer_node_addr, ":", &peer_port_str);
				peer_port = atoi(peer_port_str);

				strncpy(para->bootstrap_node_addrs[para->n_bootstrap_nodes], peer_ip, sizeof(para->bootstrap_node_addrs[para->n_bootstrap_nodes]));
				para->boostrap_node_port_nr[para->n_bootstrap_nodes]=htons(peer_port);
				para->n_bootstrap_nodes++;
				peer_node_addr = strtok_r(NULL, ",", &next_ptr);
			}

		}
	}else {
		printf("error: Not support transport %s", proto);
		free(bootstrap_info_copy);
		return -3;
	}
	free(bootstrap_info_copy);
	return 0;
}

void swarmkv_work_thread_run(struct swarmkv_store * store)
{
	unsigned int i = 0;
	for (i = 0; i < store->nr_work_threads; i++)
	{
		store->work_threads[i] = ALLOC(struct swarmkv_thread_ctx, 1);
		store->work_threads[i]->thread_id = i;
		store->work_threads[i]->evbase = event_base_new();
		store->work_threads[i]->store = store;
		pthread_create(&store->work_threads[i]->thr, NULL, swarmkv_work_thread, (void *) &(store->work_threads[i]));
	}
	return;
}

struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err)
{
	struct swarmkv_store *store = ALLOC(struct swarmkv_store, 1);
	int ret = 0;
	if ((ret = swarmkv_parse_bootstrap_info(&store->para, bootstraps)) < 0)
	{		
		*err=ALLOC(char, ERROR_STR_SIZE);
		snprintf(*err, ERROR_STR_SIZE, "Invalid bootstraps format");
		goto error_out;
	}
	
	int i = 0;
	for(i = 0; i<SLOT_NUM; i++)
	{
		store->slots[i].slot_id = i;
		store->slots[i].key_owner_node_id = 0;
//		store->slots[i].status = SLOT_STABLE;
	}
	store->nr_work_threads=2;
	swarmkv_work_thread_run(store);
	
	return store;
error_out:
	free(store);
	store=NULL;
	return NULL;
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
}

int lookup_value_owner(struct key_route_entry* hash_route_table, const char *key, size_t keylen)
{
	
}
void swarmkv_set_string(struct swarmkv_store *store,
				const char *key, size_t keylen, const char *value, size_t vallen)
{
	int ret=0, node_id;
	ret=value_owner_is_key_exisits(store->hash_own_kv, key, keylen);
	char* msg_buff=NULL;
	size_t msglen=0;
	struct swarmkv_node *key_owner=NULL;
	if(ret)//local value
	{
		swarmkv_store_value_owner_write(store->hash_own_kv, SWARMKV_CMD_SET, key, keylen, SWARMKV_DATA_TYPE_STRING, value, vallen);
	}
	else
	{
		const char* key_owner_addr = NULL;
		node_id=swarmkv_store_lookup_key_owner(store, key, keylen);
		key_owner=utarray_eltptr(store->cluster_nodes, node_id);
		swarmkv_msg_build_set_string(key, keylen, value, vallen, &msg_buff, &msglen);
		evbuffer_add(key_owner->bev, msg_buff, msglen);
		free(msg_buff);
		msg_buff=NULL;
	}
}

void swarmkv_get_string(struct swarmkv_store *store,
				const char *key, size_t keylen, swarmkv_string_type_callback_func_t *cb, void *arg)
{
	int ret=0;
	const struct kv_pair* kv=NULL;
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
		const char* key_owner_addr = NULL;
		swarmkv_store_lookup_key_owner(store, key, keylen);
		//send_cmd_to_key_owner(key_owner_addr, SWARMKV_CMD_GET, key, keylen, SWARMKV_DATA_TYPE_STRING, NULL, 0);
	}
	return;
}
int swarmkv_hset_string(struct      swarmkv_store *store, const struct swarmkv_writeoptions *options,
				const char *key, size_t keylen, const char* member, size_t memberlen, const char *value, size_t vallen)
{

}

