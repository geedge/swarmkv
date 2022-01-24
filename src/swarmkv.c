#include "swarmkv/swarmkv.h"
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
void *swarmkv_msg_processing_thread(void *arg)
{
	struct swarmkv_store *store = (struct swarmkv_store *) arg;
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

struct swarmkv_store *swarmkv_open(const char *bootstraps, const char *config, char **err)
{
	struct swarmkv_store *store = ALLOC(struct swarmkv_store, 1);
	int ret = 0;
	if ((ret = swarmkv_parse_bootstrap_info(store, bootstraps)) < 0)
	{		
		*err=ALLOC(char, ERROR_STR_SIZE);
		snprintf(*err, ERROR_STR_SIZE, "Invalid bootstraps format");
		goto error_out;
	}
	
	int i = 0;
	for(i = 0; i<SLOT_NUM; i++)
	{
		store->slots[i].slot_id = i;
		store->slots[i].owner_node_id = store->my_node_id;
		store->slots[i].status = SLOT_STABLE;
	}
	
	int pthread_ret = pthread_create(&store->tid, NULL, swarmkv_msg_processing_thread, (void*)store);
	if(pthread_ret != 0)
	{
		printf("pthread_create error: error_code = %d\n", pthread_ret);
        return NULL;
	}
	return store;
error_out:
	free(store);
	store=NULL;
	return NULL;
}

