#include "../include/swarmkv.h"
#include "../include/swarmkv_message.h"
#include "./inc_internal/uthash/uthash.h"
#include "./inc_internal/uthash/utarray.h"
#include "./inc_internal/swarmkv_utils.h"


#define BUFSIZE 2048
#define SWARMKV_DEFAULT_LISTEN_PORT 8323
#define BOOTSTRAPNODE_IP_0 "172.16.224.160"
#define BOOTSTRAPNODE_ID_0 0
#define BOOTSTRAPNODE_PORT_0 8323
#define BOOTSTRAPNODE_IP_1 "172.16.224.160"
#define BOOTSTRAPNODE_ID_1 1
#define BOOTSTRAPNODE_PORT_1 18323
#define ON 1
#define PFAIL 0
#define FAIL -1
#define SWARMKV_DEFAULT_NODE_TIMEOUT 15000000


/*struct swarmkv_readoptions
{
};
struct swarmkv_writeoptions
{
};
*/

char* get_local_ip(const char *eth_inf)
{
    int sd;
    struct sockaddr_in sin;
    struct ifreq ifr;
 
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sd)
    {
        printf("socket error: %s\n", strerror(errno));
        return NULL;
    }
    strncpy(ifr.ifr_name, eth_inf, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    if (ioctl(sd, SIOCGIFADDR, &ifr) < 0)
    {
        printf("ioctl error: %s\n", strerror(errno));
        close(sd);
        return NULL;
    }
    //printf("interfac: %s, ip: %s\n", eth_inf, inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr)); 
	char* local_ip = inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
    close(sd);
	//printf("local_ip: %s\n",local_ip);
    return local_ip;
}

int create_socket(int port)
{
	int sockfd;
	char recvbuf[BUFSIZ];
	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		perror("socket");
		return 0;
	}
	//set socket to non-block
	fcntl(sockfd, F_SETFL, O_NONBLOCK);

	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) < 0)
	{
		perror("bind");
		return 0;
	}
	return sockfd;
}

int swarmkv_send_meet_req_msg(struct swarmkv_node *bootstrap_node, struct swarmkv_store *store)
{
	struct sockaddr_in meet_addr;
	memset(&meet_addr,0,sizeof(meet_addr));
	meet_addr.sin_family = AF_INET;
	meet_addr.sin_port = htons(bootstrap_node->port);
	meet_addr.sin_addr.s_addr = inet_addr(bootstrap_node->ip);

	cJSON* js_meet_msg = 0;
	cJSON* js_node_list = 0;
	cJSON* js_node = 0;
	cJSON* js_slot_list = 0;
	cJSON* js_slot = 0;
	char* meet_msg = 0;

	js_meet_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_MEET_REQ;

	cJSON_AddNumberToObject(js_meet_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_meet_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_meet_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_meet_msg, "msg_type", msg_t);

	cJSON_AddItemToObject(js_meet_msg,"node_list", js_node_list = cJSON_CreateArray());
	struct swarmkv_node *peer_node;
	for (peer_node = store->nodes; peer_node != NULL; peer_node = (struct swarmkv_node*)(peer_node->hh.next)) {
		cJSON_AddItemToArray(js_node_list, js_node = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_node,"node_id", peer_node->node_id);
		cJSON_AddStringToObject(js_node, "node_ip", peer_node->ip);
		cJSON_AddNumberToObject(js_node,"node_port", peer_node->port);
		cJSON_AddNumberToObject(js_node,"node_status", peer_node->status);
	}
	meet_msg = cJSON_PrintUnformatted(js_meet_msg);
	if(meet_msg)
	{
		sendto(store->sockfd, meet_msg, strlen(meet_msg), 0, (struct sockaddr*)&meet_addr, sizeof(meet_addr));
		//printf("%s\n",meet_msg);
        free(meet_msg);
    }
	if(js_meet_msg)
	{
		cJSON_Delete(js_meet_msg);
	}
	
	return 1;
}

int swarmkv_send_health_check_msg(enum swarmkv_msg_type msg_t, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr;
	memset(&peer_addr,0,sizeof(peer_addr));
	peer_addr.sin_family = AF_INET;
	peer_addr.sin_port = htons(peer_node->port);
	peer_addr.sin_addr.s_addr = inet_addr(peer_node->ip);
	int i = 0;
		
	//cjson pack ping msg
	cJSON* js_ping_msg = 0;
	cJSON* js_node_list = 0;
	cJSON* js_node = 0;
	cJSON* js_slot_list = 0;
	cJSON* js_slot = 0;
	char* ping_msg = 0;
	js_ping_msg = cJSON_CreateObject();
	//enum swarmkv_msg_type msg_t = MSG_PING;
	
	cJSON_AddNumberToObject(js_ping_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_ping_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_ping_msg, "my_port", store->my_port);
	//cJSON_AddStringToObject(js_ping_msg, "msg_type", "ping");
	cJSON_AddNumberToObject(js_ping_msg, "msg_type", msg_t);
	cJSON_AddItemToObject(js_ping_msg,"node_list", js_node_list = cJSON_CreateArray());
	struct swarmkv_node* node;
	for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) {
		cJSON_AddItemToArray(js_node_list, js_node = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_node,"node_id", node->node_id);
		cJSON_AddStringToObject(js_node, "node_ip", node->ip);
		cJSON_AddNumberToObject(js_node,"node_port", node->port);
		cJSON_AddNumberToObject(js_node,"node_status", node->status);
	}
	cJSON_AddItemToObject(js_ping_msg,"slot_list", js_slot_list = cJSON_CreateArray());
	for(i=0;i<3;i++)
	{
		cJSON_AddItemToArray(js_slot_list, js_slot = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_slot,"slot_id", store->slots[i].slot_id);
		cJSON_AddNumberToObject(js_slot,"owner_id", store->slots[i].owner_node_id);
	}
	
	ping_msg = cJSON_PrintUnformatted(js_ping_msg);
    if(ping_msg)
	{
        //printf("%s\n",ping_msg);
		sendto(store->sockfd, ping_msg, strlen(ping_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
		if(msg_t == MSG_PING)
		{
			peer_node->ping_sent_t = clock();
		}
        free(ping_msg);
    }
	if(js_ping_msg)
		cJSON_Delete(js_ping_msg);
	
	return 0;
}

/*int swarmkv_parse_node_and_slot(cJSON* node_list, cJSON* slot_list, struct swarmkv_store *store)
{
	int node_num = cJSON_GetArraySize(node_list);
	cJSON *node_item;
	int i = 0;
	for (i = 0; i < node_num; i++)
	{
		node_item = cJSON_GetArrayItem(node_list, i);
		cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
		cJSON *node_status = cJSON_GetObjectItem(node_item, "node_status");
		int id = node_id->valueint;

		struct swarmkv_node *new_node = NULL;
		HASH_FIND_INT(store->nodes, &id, new_node);
		if (new_node == NULL)
		{
			printf("add a new node from ping\n");
			new_node = ALLOC(struct swarmkv_node, 1);
			new_node->node_id = id;
			cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
			cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");
			new_node->ip = node_ip->valuestring;
			new_node->port = node_port->valueint;
			new_node->status = node_status->valueint;
			new_node->ping_sent_t = 0;
			new_node->pong_received_t = 0;
			new_node->pfail_cnt = 0;
			HASH_ADD_INT(store->nodes, node_id, new_node);
		}
		//check the exist node status
		else
		{
			//if the node is failed, update the node status
			if (node_status->valueint == FAIL)
			{
				new_node->status = FAIL;
			}
			//if the node is pfailed, update the node status and increase the pfail_cnt
			if (node_status->valueint == PFAIL)
			{
				new_node->status = PFAIL;
				new_node->pfail_cnt++;
			}
		}
	}
	int slot_num = cJSON_GetArraySize(slot_list);
	cJSON *slot_item;
	for (i = 0; i < slot_num; i++)
	{
		slot_item = cJSON_GetArrayItem(slot_list, i);
		cJSON *slot_id = cJSON_GetObjectItem(slot_item, "slot_id");
		cJSON *owner_id = cJSON_GetObjectItem(slot_item, "owner_id");
		if (store->slots[slot_id->valueint].owner_node_id != owner_id->valueint)
		{
			store->slots[slot_id->valueint].owner_node_id = owner_id->valueint;
			//Haven't consider the problem of slot backup nodes.
		}
	}

	return 1;
}
*/

int swarmkv_unpack_msg(char* buf, struct swarmkv_store *store)
{
    cJSON* msg = cJSON_Parse(buf);
	if(!msg) {
    	printf("get msg faild !\n");
		return -1;
	}

	cJSON* msg_type;
	cJSON* src_id;
	cJSON* src_ip;
	cJSON* src_port;

	msg_type = cJSON_GetObjectItem(msg, "msg_type");
	src_id = cJSON_GetObjectItem(msg, "my_id");
	int id = src_id->valueint;
	src_ip = cJSON_GetObjectItem(msg, "my_ip");
	src_port = cJSON_GetObjectItem(msg, "my_port");
	
	switch(msg_type->valueint)
	{
		case MSG_PING:
		case MSG_PONG:
			if(msg_type->valueint == MSG_PING)
				printf("it is a ping msg\n");
			else
				printf("it is a pong msg\n");
			//check if the src node is legal
			struct swarmkv_node *src_node = NULL;
			//int src_node_id = src_id->valueint;
			HASH_FIND_INT(store->nodes, &id, src_node); 
			if(src_node == NULL)
			{
				printf("It is an unknown node %d!\n", id);
				return -1;
			}

			cJSON* node_list = cJSON_GetObjectItem(msg, "node_list");
			if(!node_list)
			{
				printf("no node list!\n");
				return -1;
			}
			int node_num = cJSON_GetArraySize(node_list);
			cJSON *node_item;
			int i = 0;
			for(i=0; i<node_num; i++) 
			{
				node_item = cJSON_GetArrayItem(node_list, i);
				cJSON *node_id = cJSON_GetObjectItem(node_item, "node_id");
				cJSON *node_status= cJSON_GetObjectItem(node_item, "node_status");
				int id = node_id->valueint;

				struct swarmkv_node *new_node = NULL;
				HASH_FIND_INT(store->nodes, &id, new_node); 
				if(new_node == NULL)
				{
					printf("add a new node from ping\n");
					new_node = ALLOC(struct swarmkv_node, 1);
					new_node->node_id = id;
					cJSON *node_ip = cJSON_GetObjectItem(node_item, "node_ip");
					cJSON *node_port = cJSON_GetObjectItem(node_item, "node_port");
					new_node->ip = node_ip->valuestring;
					new_node->port = node_port->valueint;
					new_node->status = node_status->valueint; 
					new_node->ping_sent_t = 0;
					new_node->pong_received_t = 0;
					new_node->pfail_cnt = 0;
					HASH_ADD_INT(store->nodes, node_id, new_node);
				}

				//check the exist node status
				else
				{
					//if the node is failed, update the node status
					if(node_status->valueint == FAIL)
					{
						new_node->status = FAIL;
					}
					//if the node is pfailed, update the node status and increase the pfail_cnt
					if(node_status->valueint == PFAIL)
					{
						new_node->status = PFAIL;
						new_node->pfail_cnt++;
					}
				}	
			}
			cJSON* slot_list = cJSON_GetObjectItem(msg, "slot_list");
			if(!node_list)
			{
				printf("no slot list!\n");
				return -1;
			}
			int slot_num = cJSON_GetArraySize(slot_list);
			cJSON *slot_item;
			for(i=0; i<slot_num; i++) 
			{
				slot_item = cJSON_GetArrayItem(slot_list, i);
				cJSON *slot_id = cJSON_GetObjectItem(slot_item, "slot_id");
				cJSON *owner_id= cJSON_GetObjectItem(slot_item, "owner_id");
				if(store->slots[slot_id->valueint].owner_node_id != owner_id->valueint)
				{
					store->slots[slot_id->valueint].owner_node_id = owner_id->valueint;
					//Haven't consider the problem of slot backup nodes.
				}
			}
			if(msg_type->valueint == MSG_PING)
			{
				swarmkv_send_health_check_msg(MSG_PONG, src_node, store);
			}
			else
			{
				swarmkv_send_health_check_msg(MSG_PING, src_node, store);
			}
			//send a pong/ping msg
			break;

		case MSG_MEET_REQ:
			printf("it is a meet msg\n");
			//int id = src_id->valueint;
			struct swarmkv_node *new_node;
    		HASH_FIND_INT(store->nodes, &id, new_node); 
			if(new_node == NULL)
			{
				printf("meet a new node\n");
				new_node = ALLOC(struct swarmkv_node, 1);
				new_node->node_id = id;
				new_node->ip = src_ip->valuestring;
				new_node->port = src_port->valueint;
				new_node->ping_sent_t = 0;
				new_node->pong_received_t = 0;
				new_node->status = 1;
				new_node->pfail_cnt = 0;
				HASH_ADD_INT(store->nodes, node_id, new_node);
			}
			printf("no new node\n");
			//send a meet response msg
			swarmkv_send_health_check_msg(MSG_MEET_RESP, new_node, store);

			break;

		case MSG_MEET_RESP:
			printf("it is a meet msg\n");
			//int id = src_id->valueint;
			struct swarmkv_node *bootstrap_node;
    		HASH_FIND_INT(store->nodes, &id, bootstrap_node); 
			if(bootstrap_node == NULL)
			{
				printf("it is an unkonwn bootstrap node\n");
				return -1;
			}
			bootstrap_node->status = ON;
			//send a migrate msg
			break;
		default:
			printf("unknown msg\n");
	}

	return 1;
}


//check cluster nodes status
void swarmkv_node_health_check(int sockfd, struct swarmkv_store *store)
{
	int nodes_cnt;
	int i = 0;
	nodes_cnt = HASH_COUNT(store->nodes);
	//printf("%d\n", nodes_cnt);
	uint32_t min_pong = 0;
	struct swarmkv_node *min_pong_node = NULL;

	//select a node to send ping msg
	int choose_in = 5;
	if(nodes_cnt < choose_in)
	{
		choose_in=nodes_cnt;
	}
	for (i = 0; i < choose_in; i++)
	{
		int node_id = rand()%nodes_cnt;
		struct swarmkv_node *candidate_node = NULL;
		candidate_node = ALLOC(struct swarmkv_node, 1);
		HASH_FIND_INT(store->nodes, &node_id, candidate_node);
		if(candidate_node == NULL)
		{
			continue;
		}
		if(candidate_node->ping_sent_t!=0)
		{
			//recently sent a ping, and did not receive the corresponding pong.
			continue;
		}
		if(min_pong_node == NULL || min_pong > candidate_node->pong_received_t)
		{
			min_pong = candidate_node->pong_received_t;
			min_pong_node = candidate_node;
		}
	}
	if(min_pong_node)
	{
		printf("send a ping to random node %d\n", min_pong_node->node_id);
		swarmkv_send_health_check_msg(MSG_PING, min_pong_node, store);
	}
}

int add_bootstrap_node(struct swarmkv_store *store)
{
	//add bootstraps
	struct swarmkv_node* bootstrap_nodes = NULL;
	//bootstrap node 0
	struct swarmkv_node* bootstrap_node0 = NULL;
	bootstrap_node0 = ALLOC(struct swarmkv_node, 1);
	bootstrap_node0->node_id = BOOTSTRAPNODE_ID_0;
	bootstrap_node0->ip = BOOTSTRAPNODE_IP_0;
	bootstrap_node0->port = BOOTSTRAPNODE_PORT_0;
	bootstrap_node0->ping_sent_t = 0;
	bootstrap_node0->pong_received_t = 0;
	bootstrap_node0->status = FAIL;
	bootstrap_node0->pfail_cnt = 0;
	
	////bootstrap node 1
	struct swarmkv_node* bootstrap_node1 = NULL;
	bootstrap_node1 = ALLOC(struct swarmkv_node, 1);
	bootstrap_node1->node_id = BOOTSTRAPNODE_ID_1;
	bootstrap_node1->ip = BOOTSTRAPNODE_IP_1;
	bootstrap_node1->port = BOOTSTRAPNODE_PORT_1;
	bootstrap_node1->ping_sent_t = 0;
	bootstrap_node1->pong_received_t = 0;
	bootstrap_node1->status = FAIL;
	bootstrap_node1->pfail_cnt = 0;

	if((!strcmp(store->my_ip, BOOTSTRAPNODE_IP_0)) && (store->my_port==BOOTSTRAPNODE_PORT_0))
	{
		HASH_ADD_INT(bootstrap_nodes, node_id, bootstrap_node1); 
	}
	else if((!strcmp(store->my_ip, BOOTSTRAPNODE_IP_1)) && (store->my_port==BOOTSTRAPNODE_PORT_1))
	{
		HASH_ADD_INT(bootstrap_nodes, node_id, bootstrap_node0); 
	}
	else
	{
		HASH_ADD_INT(bootstrap_nodes, node_id, bootstrap_node0); 
		HASH_ADD_INT(bootstrap_nodes, node_id, bootstrap_node1); 
	}
	store->nodes = bootstrap_nodes;

	return 1;
}

struct swarmkv_store *swarmkv_open(uint16_t port, const char *eth_inf, int my_node_id)
{
	printf("open a node\n");
	struct swarmkv_store *store = NULL;
	store = ALLOC(struct swarmkv_store, 1);
	//my info
	store->my_ip = get_local_ip(eth_inf);
	//printf("%s\n",store->my_ip);
	store->my_node_id = my_node_id;
	if(!port)
		store->my_port = SWARMKV_DEFAULT_LISTEN_PORT;
	else
		store->my_port = port;
	
	//create socket
	struct sockaddr_in peer_addr;
    socklen_t peerlen = sizeof(struct sockaddr_in);
	char recvbuf[BUFSIZ];
	store->sockfd = create_socket(store->my_port);
	printf("store->sockfd : %d\n", store->sockfd);

	add_bootstrap_node(store);
	//send meet msg to bootstrap nodes
	struct swarmkv_node* bootstrap_node = NULL;
	for (bootstrap_node = store->nodes; bootstrap_node != NULL; bootstrap_node = (struct swarmkv_node*)(bootstrap_node->hh.next)) 
	{
		swarmkv_send_meet_req_msg(bootstrap_node, store);
		//printf("send meet msg\n");
	}

	int msg_cnt = 0;
	int ping_cnt = 0;
	int check_cnt = 0;
	while(1)
	{
		memset(recvbuf, 0, sizeof(recvbuf));
		int n = recvfrom(store->sockfd, recvbuf, BUFSIZ, 0, (struct sockaddr *)&peer_addr, &peerlen);
		if(n > 0) 
		{
			/*
			msg=parse_msg();
            switch(msg->type)
            {
                //handle put, get, ping and etc.
            }
			*/
			//msg_cnt++;
			//printf("recieve %d msg\n", msg_cnt);
			//printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
			swarmkv_unpack_msg(recvbuf, store);	
		}
		
		clock_t cur_t = clock();
		struct swarmkv_node* node = NULL;
		//Traverse all nodes per 100ms
		//int nodes_cnt = HASH_COUNT(store->nodes);
		//printf("%d\n", nodes_cnt);
		if(cur_t/100000 >= check_cnt)
		{
			check_cnt++;
			for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) 
			{
				if(node->status != FAIL)
				{
				if(node->pong_received_t == 0)
				{
					//printf("send a ping to node %d\n", node->node_id);
					swarmkv_send_health_check_msg(MSG_PING, node, store);
				}
				//if there is a node: current_time - pong_sent_t >timeout/2, send a ping msg immediately
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT/2)
				{
					printf("send a ping to node %d\n", node->node_id);
					swarmkv_send_health_check_msg(MSG_PING, node, store);
				}
				//if there is a node: current_time - pong_sent_t >timeout, suspect that the node is failed
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT)
				{
					printf("set a node %d pfail\n", node->node_id);
					node->status = PFAIL;
				}
				}
    		}
		}
		//ping a random node per second
		if(cur_t/1000000 >= ping_cnt)
		{
			//printf("select a random node\n");
			ping_cnt++;
			swarmkv_node_health_check(store->sockfd, store);
		}	
	}
	close(store->sockfd);
	return store;
	//1 create udp socket, which receive and send msg via UDP 8323 by default. And necessary data structures.
	//2 Get slot table, choose slots for myself
	//3 Key Migration
	// 3.1 Send slot moving request to original slot master;
	// 3.2 For original slot master:
	//	- Send the keys of this slot to new slot master;
	//  - Mark this slot moved, read/write operations of this slot are redirected to new slot master
	//  Note: each slot is small, so that we can block r/w on sending keys.
	// 3.3 Broadcast slot master changed message in cluster
	//4 Create a thread to handle remote messages: PUT, GET, MOVE, HEALTH_CHECK, INVALIDATE_KEY, LIST_SLOT
	//return
}

/*
char *swarmkv_get(struct swarmkv_store *store, const char *table_name, const struct swarmkv_readoptions *options, const char *key, size_t keylen, size_t *vallen, char **errptr)
{
	int slot_id = -1;
	struct swarmkv_slot *slot = NULL;
	struct swarmkv_table *table = NULL;
	struct swarmkv_value *result = NULL;
	struct swarmkv_transaction *transaction = NULL;
	char *ret_buff = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	slot_id = crc(key, keylen);
	slot = store->slots[slot_id];
	assert(slot_id = slot->slot_id);
	if (slot->owner_node_id == store->node_id) //if I am the owner of this slot
	{
		table = HASH_FIND_STR(&store->table_hash, table_name);
		if (!table)
		{
			//table=swarmkv_table_new();
		}
		HASH_FIND_PTR(table->owned_value_hash, key, keylen, &result);
		if (result)
		{
			ret_buff = ALLOC(char *, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
	}
	else
	{
		transaction = ALLOC(struct swarmkv_transaction, 1);
		transaction->uuid = getuuid();
		HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
		//call rpc
	}
	//先检查缓存？
	else
	{
		table = HASH_FIND_STR(&store->table_hash, table_name);
		HASH_FIND_PTR(table->cached_value_hash, key, keylen, &result);
		if (result)
		{
			ret_buff = ALLOC(char *, result->vallen);
			memcpy(ret_buff, result->value, result->vallen);
		}
		else
		{
			transaction = ALLOC(struct swarmkv_transaction, 1);
			transaction->uuid = getuuid();
			HASH_ADD(hh, store->on_fly_transactions, uuid, strlen(transaction->uuid), transaction);
			//call rpc
		}
	}
	pthread_rwlock_unlock(&store->rwlock);
}
*/

void swarmkv_close()
{
	//Broadcast LEAVE message to cluster
	//Free data structures
	//return;
}

/*
void swarmkv_msg_processing_thread()
{
	while (1)
	{
		receive_msg(sd, buff);
		msg = msgpack_unpack(buff);
		switch (msg.type)
		{
		case PUT_REQ:
			//hash_table_add(owned_value_hash, msg.key, msg.value)
			//send INVLIDATE_KEY message
			break;
		case GET_REQ:
			//value=hash_table_find(owned_value_hash, msg.key);
			//update nodes_have_key(msg.node_id,);
			//send GET_RESPONSE message
			break;
		case GET_RESPONSE:
		case PUT_RESPONSE:
			//hash_table_add(cached_value_hash, msg.key, msg.value);
			//transaction=hash_table_find(on_fly_transactions, msg.transaction_id);
			//transaction.callback()
			break;
		case HEALTH_CHECK_PING:
			//check swarmkv_node nodes;
			//send HEALTH_CHECK_PONG message;
			break;
		case HEALTH_CHECK_PONG:
			//update swarmkv_node->pong_recieved;
			//check swarmkv_node nodes;
			break;
		case MEET_REQ:
			//add the node into swarmkv_store->nodes
			//send MEET_RESP message
			break;
		case MEET_RESP:
			//read swarmkv_slot myslots[65535];
			break;
		case FAILED:
			//calculate consistent hash, update slot table
			break;
		case MOVE_REQ:
			//see swarmkv_open 3.2
			break;
		case MOVE_RESP:
			//update swarmkv_value* owned_value_hash;
			break;
		case HEALTH_CHECK:
			//do gossip
			break;
		case INVALIDATE_KEY:
			//delete from cached_value_hash;
			break;
		}
		//Failover of cluster node
		//send failed_msg to other node
		//read swarmkv_store->slots, find the slots belonging to this failed node;
		//calculate consistent hash, update slot table
		//Notify other nodes of the new slot table?或者仅仅是通知其他节点我发现哪个节点挂了，让他们自己重新计算slot table？
	}
}
*/

int main(int argc, char **argv)
{
	//printf("%d\n",argc);
	if(argc == 4)
	{
		int port = atoi(argv[1]);
		//printf("%s\n",argv[2]);
		char *eth_inf = argv[2];
		int node_id = atoi(argv[3]);
		swarmkv_open(port,eth_inf,node_id);
	}
	else
	{
		printf("arg error\n");
	}

	return 1;
}