#pragma once

#include<msgpack.h>
#include<stdio.h>
#include<time.h>
#include<fcntl.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<assert.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <fcntl.h>
#include <errno.h> 
#include <net/if.h>  
#include <netdb.h>  
#include <sys/types.h>  
#include <sys/ioctl.h> 
#include <cjson/cJSON.h> 

#include "../src/inc_internal/uthash/uthash.h"
#include "../src/inc_internal/uthash/utarray.h"


#ifdef __cplusplus
extern "C" {
#endif

struct swarmkv_value
{
	char *key;
	size_t keylen;
	char *value;
	size_t vallen;
	UT_array *nodes_have_key_cached; //for key invalidation
	UT_hash_handle hh;
};

struct swarmkv_slot
{
	int slot_id;
	int owner_node_id;
	int replica_node_id[16];
};

struct swarmkv_table
{
	char table_name[256];		 //Key
	//int update_interval;   //bin log?
	int cache_invalidation_mode; //Tracking or broadcasting
	struct swarmkv_value *owned_value_hash;
	struct swarmkv_value *cached_value_hash;
	pthread_rwlock_t rwlock;
	UT_hash_handle hh;
};

struct swarmkv_node
{
	int node_id;       //Key
	char* ip;
	uint16_t port;
	uint32_t ping_sent_t;
	uint32_t pong_received_t;
	int status;        //ONLINE/PFAIL/FAIL
	int pfail_cnt;
	UT_hash_handle hh;
};

struct swarmkv_store
{
	int my_node_id; 
	char* my_ip;
	uint16_t my_port;  
	struct swarmkv_slot slots[65535];
	struct swarmkv_table *table_hash; //key: char table_name[256], value: struct swarmkv_table*
	struct swarmkv_node *nodes;		  //key: int node_id, value: struct swarmkv_node*
	int sockfd;
	struct swarmkv_transaction *on_fly_transactions;
	pthread_rwlock_t rwlock;
};

int create_socket(int port);
//void swarmkv_node_health_check(int sockfd, struct swarmkv_store *store);
struct swarmkv_store *swarmkv_open(uint16_t port, const char *eth_inf, int my_node_id);
//void swarmkv_close();
//void swarmkv_msg_processing_thread();
//int swarmkv_send_health_check_msg(enum swarmkv_msg_type msg_t, struct swarmkv_node* peer_node, struct swarmkv_store *store);

// config example: "node_id=1;token=49ffb3ae-8e7e-40ab-bc3f-824567e932fe,db=0";
// bootstraps example: "self=0.0.0.0:8323;peers=tcp://192.168.0.100:8323,tcp://192.168.0.101:8323"


#ifdef __cplusplus
}  /* end extern "C" */
#endif

