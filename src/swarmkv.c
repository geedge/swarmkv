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

static const uint32_t crc32tab[] = {
 0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL,
 0x076dc419L, 0x706af48fL, 0xe963a535L, 0x9e6495a3L,
 0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L,
 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L, 0x90bf1d91L,
 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
 0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L,
 0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
 0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L,
 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L, 0xa2677172L,
 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
 0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L,
 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L,
 0x26d930acL, 0x51de003aL, 0xc8d75180L, 0xbfd06116L,
 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL,
 0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL,
 0x71b18589L, 0x06b6b51fL, 0x9fbfe4a5L, 0xe8b8d433L,
 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L,
 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
 0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
 0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L,
 0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL, 0xfcb9887cL,
 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L,
 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL,
 0x4369e96aL, 0x346ed9fcL, 0xad678846L, 0xda60b8d0L,
 0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
 0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L,
 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
 0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L,
 0x59b33d17L, 0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL,
 0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL,
 0xead54739L, 0x9dd277afL, 0x04db2615L, 0x73dc1683L,
 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
 0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L,
 0xf00f9344L, 0x8708a3d2L, 0x1e01f268L, 0x6906c2feL,
 0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L,
 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL, 0x67dd4accL,
 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
 0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L,
 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
 0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L,
 0xdf60efc3L, 0xa867df55L, 0x316e8eefL, 0x4669be79L,
 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
 0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL,
 0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L,
 0xc2d7ffa7L, 0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL,
 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
 0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L,
 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L,
 0x86d3d2d4L, 0xf1d4e242L, 0x68ddb3f8L, 0x1fda836eL,
 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L,
 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
 0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
 0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L,
 0xa7672661L, 0xd06016f7L, 0x4969474dL, 0x3e6e77dbL,
 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L,
 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
 0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L,
 0xbad03605L, 0xcdd70693L, 0x54de5729L, 0x23d967bfL,
 0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL 
};

uint32_t crc32( const unsigned char *buf, uint32_t size)
{
     uint32_t i, crc;
     crc = 0xFFFFFFFF;
     for (i = 0; i < size; i++)
      crc = crc32tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
     return crc^0xFFFFFFFF;
}

int cmp_by_hash_val(const void* a, const void* b)
{
	if(((const struct swarmkv_ch_node*)a)->ch_hash_val > ((const struct swarmkv_ch_node*)b)->ch_hash_val)
	{
		return 1;
	}
	else if(((const struct swarmkv_ch_node*)a)->ch_hash_val == ((const struct swarmkv_ch_node*)b)->ch_hash_val)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

//ConsistentHashing
int swarmkv_consistent_hashing_create(struct swarmkv_store* store)
{
	int node_cnt = 0;
	struct swarmkv_node *peer_node = NULL;
	for (peer_node = store->nodes; peer_node != NULL; peer_node = (struct swarmkv_node*)(peer_node->hh.next)) 
	{
		node_cnt++;
		node_cnt += peer_node->virtual_num;
	}
	//struct swarmkv_ch_node* ch_nodes;
	//ch_nodes = ALLOC(struct swarmkv_ch_node, node_cnt);
	int num = 0;
	int i = 0;
	for (peer_node = store->nodes; peer_node != NULL; peer_node = (struct swarmkv_node*)(peer_node->hh.next)) 
	{
		for(i=0; i<peer_node->virtual_num; i++)
		{
			char* node_key = ALLOC(char, 20);
			sprintf(node_key, "%s : %d", peer_node->ip, peer_node->port);
			int key_len = strlen(node_key);
			store->ch_nodes[num].node_id = peer_node->node_id;
			store->ch_nodes[num].ch_hash_val = crc32(node_key, key_len)%SWARMKV_DEFAULT_SLOT_NUM;
			num++;
		}
	}
	qsort(store->ch_nodes, node_cnt, sizeof(struct swarmkv_ch_node),cmp_by_hash_val);	

	return 1;
}

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

//set node address
struct sockaddr_in set_node_addr(struct swarmkv_node* node)
{
	struct sockaddr_in addr;
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(node->port);
	addr.sin_addr.s_addr = inet_addr(node->ip);

	return addr;
}

//meet_req msg
int swarmkv_send_meet_req_msg(struct swarmkv_node *bootstrap_node, struct swarmkv_store *store)
{
	struct sockaddr_in meet_addr = set_node_addr(bootstrap_node);
	/*
	memset(&meet_addr,0,sizeof(meet_addr));
	meet_addr.sin_family = AF_INET;
	meet_addr.sin_port = htons(bootstrap_node->port);
	meet_addr.sin_addr.s_addr = inet_addr(bootstrap_node->ip);
	*/

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

/*
cJSON* swarmkv_pack_node_list(struct swarmkv_store *store)
{
	cJSON* js_node_list = 0;
	cJSON* js_node = 0;
	
	js_node_list = cJSON_CreateArray();
	struct swarmkv_node* node;
	for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) {
		cJSON_AddItemToArray(js_node_list, js_node = cJSON_CreateObject());
		cJSON_AddNumberToObject(js_node,"node_id", node->node_id);
		cJSON_AddStringToObject(js_node, "node_ip", node->ip);
		cJSON_AddNumberToObject(js_node,"node_port", node->port);
		cJSON_AddNumberToObject(js_node,"node_status", node->status);
	}
	
	return js_node_list;
}
*/

//ping, pong, meet_resp msg
int swarmkv_send_health_check_msg(enum swarmkv_msg_type msg_t, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
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
	//js_node_list = swarmkv_pack_node_list(store);
	//cJSON_AddItemToObject(js_ping_msg,"node_list", js_node_list);
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
        //printf("****msg:\n%s\n",ping_msg);
		sendto(store->sockfd, ping_msg, strlen(ping_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
		if(msg_t == MSG_PING)
		{
			peer_node->ping_sent_t = clock();
		}
        free(ping_msg);
    }
	if(js_ping_msg)
		cJSON_Delete(js_ping_msg);
	
	return 1;
}

//fail msg
int swarmkv_broadcast_fail_msg(int fail_node_id, struct swarmkv_store *store)
{
	cJSON* js_fail_msg = 0;
	char* fail_msg = 0;
	js_fail_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_FAIL_REQ;
	struct swarmkv_node* node;
	
	cJSON_AddNumberToObject(js_fail_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_fail_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_fail_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_fail_msg, "msg_type", msg_t);
	cJSON_AddNumberToObject(js_fail_msg, "fail_node_id", fail_node_id);

	fail_msg = cJSON_PrintUnformatted(js_fail_msg);
    if(fail_msg)
	{
		//broadcast
		for (node = store->nodes; node != NULL; node = (struct swarmkv_node*)(node->hh.next)) 
		{
			struct sockaddr_in peer_addr = set_node_addr(node);
			sendto(store->sockfd, fail_msg, strlen(fail_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
		}
		free(fail_msg);
	}
	if(js_fail_msg)
		cJSON_Delete(js_fail_msg);

	return 1;
}

//only response "ok"
int ok_response(enum swarmkv_msg_type msg_t, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
		
	//cjson pack ping msg
	cJSON* js_resp_msg = 0;
	char* resp_msg = 0;
	js_resp_msg = cJSON_CreateObject();
	
	cJSON_AddNumberToObject(js_resp_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_resp_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_resp_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_resp_msg, "msg_type", msg_t);
	cJSON_AddStringToObject(js_resp_msg, "msg", "ok");

	resp_msg = cJSON_PrintUnformatted(js_resp_msg);
    if(resp_msg)
	{
        //printf("%s\n",resp_msg);
		sendto(store->sockfd, resp_msg, strlen(resp_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(resp_msg);
    }
	if(js_resp_msg)
		cJSON_Delete(js_resp_msg);

	return 1;
}

//pack key-value to a cjson array [{"tb_name":"tb1", "key":"k1", "value":"v1"} {"table_name": }]
cJSON* swarmkv_pack_kv(int num, char* tb_name[], char* key[], char* value[])
{
	cJSON* js_kv_list = 0;
	js_kv_list = cJSON_CreateArray();
	cJSON* kv_item = 0;
	int i = 0;
	for(i=0; i<num; i++)
	{
		cJSON_AddItemToArray(js_kv_list, kv_item = cJSON_CreateObject());
		cJSON_AddStringToObject(kv_item,"table_name", tb_name[i]);
		cJSON_AddStringToObject(kv_item,"key", key[i]);
		cJSON_AddStringToObject(kv_item, "value", value[i]);
	}
	return js_kv_list;
}

int swarmkv_send_invalidate_keys_req(char* tb_name, char* key, char* value, struct swarmkv_node* peer_node, struct swarmkv_store *store )
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);

	//cjson pack ping msg
	cJSON* js_invalidate_key_req = 0;
	char* invalidate_key_req = 0;
	js_invalidate_key_req = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_INVALIDATE_KEYS_REQ;
	
	cJSON_AddNumberToObject(js_invalidate_key_req, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_invalidate_key_req, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_invalidate_key_req, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_invalidate_key_req, "msg_type", msg_t);
	cJSON_AddStringToObject(js_invalidate_key_req, "table_name", tb_name);
	cJSON_AddStringToObject(js_invalidate_key_req,"key", key);
	cJSON_AddStringToObject(js_invalidate_key_req, "value", value);

	invalidate_key_req = cJSON_PrintUnformatted(js_invalidate_key_req);
    if(invalidate_key_req)
	{
		sendto(store->sockfd, invalidate_key_req, strlen(invalidate_key_req), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(invalidate_key_req);
    }
	if(js_invalidate_key_req)
		cJSON_Delete(js_invalidate_key_req);

	return 1;
}

//put one key-value into my store and send an invalidate request msg
int swarmkv_put_one_kv(char* tb_name, char* key, char* value, struct swarmkv_store *store)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_value *kv = NULL;
	pthread_rwlock_wrlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//add a new table in my swarmkv_store
		table = ALLOC(struct swarmkv_table, 1);
		memcpy(table->table_name, tb_name, strlen(tb_name));
		HASH_ADD_STR(store->table_hash, table_name, table);
		//add a new key
		kv = ALLOC(struct swarmkv_value, 1);
		kv->key = key;
		HASH_ADD_STR(table->owned_value_hash, key, kv);
	}
	HASH_FIND_STR(table->owned_value_hash, key, kv);
	if(kv == NULL)
	{
		//add a new key
		kv = ALLOC(struct swarmkv_value, 1);
		kv->key = key;
		HASH_ADD_STR(table->owned_value_hash, key, kv);
	}
	kv->keylen = strlen(kv->key);
	kv->value = value;
	kv->vallen = strlen(kv->value);

	//send invalidate msg to each node in the nodes_have_key_cached
	int* cached_node_id;
	struct swarmkv_node* cache_node = NULL;
	for(cached_node_id=(int*)utarray_front(kv->nodes_have_key_cached); cached_node_id!=NULL; cached_node_id=(int*)utarray_next(kv->nodes_have_key_cached,cached_node_id)) 
	{
		HASH_FIND_INT(store->nodes, cached_node_id, cache_node);
		if(cache_node != NULL)
		{
			swarmkv_send_invalidate_keys_req(tb_name, key, value, cache_node, store);
		}
	}
	pthread_rwlock_unlock(&store->rwlock);

	return 1;
}

//put req msg 
//only one kv per time
int swarmkv_send_put_req_msg(char* tb_name, char* key, char* value, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
		
	//cjson pack ping msg
	cJSON* js_put_req_msg = 0;
	char* put_req_msg = 0;
	js_put_req_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_PUT_REQ;
	
	cJSON_AddNumberToObject(js_put_req_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_put_req_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_put_req_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_put_req_msg, "msg_type", msg_t);
	cJSON_AddStringToObject(js_put_req_msg, "table_name", tb_name);
	cJSON_AddStringToObject(js_put_req_msg,"key", key);
	cJSON_AddStringToObject(js_put_req_msg, "value", value);
	
	put_req_msg = cJSON_PrintUnformatted(js_put_req_msg);
    if(put_req_msg)
	{
        //printf("%s\n",put_req_msg);
		sendto(store->sockfd, put_req_msg, strlen(put_req_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(put_req_msg);
    }
	if(js_put_req_msg)
		cJSON_Delete(js_put_req_msg);

	return 1;
}
/*
//multiple key-value
int swarmkv_send_put_req_msg(int num, char* tb_name[], char* key[], char* value[], struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
		
	//cjson pack ping msg
	cJSON* js_put_req_msg = 0;
	char* put_req_msg = 0;
	js_put_req_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_PUT_REQ;
	
	cJSON_AddNumberToObject(js_put_req_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_put_req_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_put_req_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_put_req_msg, "msg_type", msg_t);

	cJSON* js_kv_list = swarmkv_pack_kv(num, tb_name, key, value);
	cJSON_AddItemToObject(js_put_req_msg,"kv", js_kv_list);

	put_req_msg = cJSON_PrintUnformatted(js_put_req_msg);
    if(put_req_msg)
	{
        //printf("%s\n",put_req_msg);
		sendto(store->sockfd, put_req_msg, strlen(put_req_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(put_req_msg);
    }
	if(js_put_req_msg)
		cJSON_Delete(js_put_req_msg);

	return 1;
}
*/

int caculate_key_slot_owner(char* key, struct swarmkv_store *store)
{
	struct swarmkv_slot slot;
	int slot_id = -1;
	slot_id = crc32(key,strlen(key))%SWARMKV_DEFAULT_SLOT_NUM;
	slot = store->slots[slot_id];
	assert(slot_id == slot.slot_id);
	return slot.owner_node_id;
}

//put
int swarmkv_put(char* tb_name, char* key, char* value, struct swarmkv_store *store)
{
	int slot_owner_id = -1;
	slot_owner_id = caculate_key_slot_owner(key, store);
	if(slot_owner_id == store->my_node_id)
	{
		//local put a key-value
		swarmkv_put_one_kv(tb_name, key, value, store);
	}
	else
	{
		struct swarmkv_node* peer_node = NULL;
		HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
		if(peer_node == NULL)
		{
			printf("I can't find the owner_node of the key %s!\n", key);
			return -1;
		}
		swarmkv_send_put_req_msg(tb_name, key, value, peer_node, store);
	}
	return 1;
}
/*
int swarmkv_put(int num, char* tb_name[], char* key[], char* value[], struct swarmkv_store *store)
{
	int i = 0;
	int slot_owner_id = -1;
	for(i=0; i<num; i++)
	{
		int slot_owner_id = caculate_key_slot_owner(key[i], store);
		if(slot_owner_id == store->my_node_id)
		{
			//local put a key-value
			swarmkv_put_one_kv(tb_name[i], key[i], value[i], store);
		}
		else
		{
			struct swarmkv_node* peer_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
			if(peer_node == NULL)
			{
				printf("I can't find the owner_node of the key %s!\n", key[i]);
				continue;
			}
			swarmkv_send_put_req_msg(tb_name[i], key[i], value[i], peer_node, store);
		}
	}
	return 1;
}
*/

//get req msg
int swarmkv_send_get_req(char *table_name, char* key, char* cb_function, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
		
	//cjson pack ping msg
	cJSON* js_get_req_msg = 0;
	char* get_req_msg = 0;
	js_get_req_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_GET_REQ;
	
	cJSON_AddNumberToObject(js_get_req_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_get_req_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_get_req_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_get_req_msg, "msg_type", msg_t);
	cJSON_AddStringToObject(js_get_req_msg, "callback", cb_function);
	cJSON_AddStringToObject(js_get_req_msg, "table_name", table_name);
	cJSON_AddStringToObject(js_get_req_msg, "key", key);

	get_req_msg = cJSON_PrintUnformatted(js_get_req_msg);
    if(get_req_msg)
	{
        //printf("%s\n",get_req_msg);
		sendto(store->sockfd, get_req_msg, strlen(get_req_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(get_req_msg);
    }
	if(js_get_req_msg)
		cJSON_Delete(js_get_req_msg);

	return 1;
}

//get resp msg
int swarmkv_send_get_resp(char* key, char* value, char* cb_function, struct swarmkv_node* peer_node, struct swarmkv_store *store)
{
	struct sockaddr_in peer_addr = set_node_addr(peer_node);
		
	//cjson pack ping msg
	cJSON* js_get_resp_msg = 0;
	char* get_resp_msg = 0;
	js_get_resp_msg = cJSON_CreateObject();
	enum swarmkv_msg_type msg_t = MSG_GET_RESP;
	
	cJSON_AddNumberToObject(js_get_resp_msg, "my_id", store->my_node_id);
	cJSON_AddStringToObject(js_get_resp_msg, "my_ip", store->my_ip);
	cJSON_AddNumberToObject(js_get_resp_msg, "my_port", store->my_port);
	cJSON_AddNumberToObject(js_get_resp_msg, "msg_type", msg_t);
	cJSON_AddStringToObject(js_get_resp_msg, "callback", cb_function);
	cJSON_AddStringToObject(js_get_resp_msg, "key", key);
	cJSON_AddStringToObject(js_get_resp_msg, "value", value);

	get_resp_msg = cJSON_PrintUnformatted(js_get_resp_msg);
    if(get_resp_msg)
	{
        //printf("%s\n",get_resp_msg);
		sendto(store->sockfd, get_resp_msg, strlen(get_resp_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        free(get_resp_msg);
    }
	if(js_get_resp_msg)
		cJSON_Delete(js_get_resp_msg);

	return 1;
}

char* swarmkv_get_one_kv(char* tb_name, char* key, int req_node, struct swarmkv_store *store)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_value *kv = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I can't find the table!\n");
		return NULL;
	}
	HASH_FIND_STR(table->owned_value_hash, key, kv);
	if(kv == NULL)
	{
		//printf("I can't find the key!\n");
		return NULL;
	}
	//在UT_array *nodes_have_key_cached里插入这个查询的节点
	if(req_node != store->my_node_id)
	{
		utarray_push_back(kv->nodes_have_key_cached, &req_node);
	}
	pthread_rwlock_unlock(&store->rwlock);
	return kv->value;
}

char* swarmkv_get_one_kv_in_cache(char* tb_name, char* key, struct swarmkv_store *store)
{
	struct swarmkv_table *table = NULL;
	struct swarmkv_value *kv = NULL;
	pthread_rwlock_rdlock(&store->rwlock);
	HASH_FIND_STR(store->table_hash, tb_name, table);
	if(table == NULL)
	{
		//printf("I can't find the table in my cache!\n");
		return NULL;
	}
	HASH_FIND_STR(table->cached_value_hash, key, kv);
	if(kv == NULL)
	{
		//printf("I can't find the key in my cache!\n");
		return NULL;
	}
	pthread_rwlock_unlock(&store->rwlock);
	return kv->value;
}

//get keys
char* swarmkv_get(char* tb_name, char* key, char* cb_function, struct swarmkv_store *store)
{
	int slot_owner_id = -1;
	char* value = NULL;
	
	slot_owner_id = caculate_key_slot_owner(key, store);
	
	//I am the owner of the key
	if(slot_owner_id == store->my_node_id)
	{
		value = swarmkv_get_one_kv(tb_name, key, slot_owner_id, store);
		//printf("%s: %s\n", key[i], value);
	}
	else
	{
		//check my cache
		value = swarmkv_get_one_kv_in_cache(tb_name, key, store);
		//I have not cache the kv
		if(value == NULL)
		{
			//send a get request
			struct swarmkv_node* peer_node = NULL;
			HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
			if(peer_node == NULL)
			{
				printf("I can't find the owner_node of the key %s!\n", key);	
			}
			swarmkv_send_get_req(tb_name, key, cb_function, peer_node, store);
		}
	}
	return value;
}
/*
int swarmkv_get(int num, char* tb_name[], char* key[], char* cb_function, struct swarmkv_store *store)
{
	int i = 0;
	int slot_owner_id = -1;
	char* value[num] = NULL;
	for(i=0; i<num; i++)
	{
		value[i] = NULL;
		int slot_owner_id = caculate_key_slot_owner(key[i], store);
		//local get a key-value
		//I am the owner of the key
		if(slot_owner_id == store->my_node_id)
		{
			value[i] = swarmkv_get_one_kv(tb_name[i], key[i], store);
			//printf("%s: %s\n", key[i], value);
		}
		else
		{
			//check my cache
			value[i] = swarmkv_get_one_kv_in_cache(tb_name[i], key[i], store);
			//I have not cache the kv
			if(value[i] == NULL)
			{
				//send a get request
				struct swarmkv_node* peer_node = NULL;
				HASH_FIND_INT(store->nodes, &slot_owner_id, peer_node);
				if(peer_node == NULL)
				{
					printf("I can't find the owner_node of the key!\n");
					continue;
				}
				swarmkv_send_get_req(tb_name[i], key[i], cb_function, peer_node, store);
			}
		}
	}
	return 1;
}
*/

//parse the node list and slot list in ping/pong/meet_resp msg
int swarmkv_parse_node_and_slot(cJSON* node_list, cJSON* slot_list, struct swarmkv_store *store)
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
		//add new nodes into my store->nodes
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
				
				//Check if more than half of the nodes think that the current node is pfail
				int node_cnt = HASH_COUNT(store->nodes);
				if(new_node->pfail_cnt > node_cnt/2)
				{
					new_node->status = FAIL;
					new_node->pfail_cnt = 0;
					//broadcast a fail msg
					swarmkv_broadcast_fail_msg(id, store);
				}			
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
			//需要添加epoch进行控制，不然不知道到底谁的版本才是最新的，可能因为网络延迟，旧的slot版本反而后到
		}
	}
	return 1;
}

/*
//put a list of key-value pairs into my store and send invalidate request msg
int swarmkv_put_kv(cJSON* kv_list, int kv_num, struct swarmkv_store *store)
{
	cJSON *kv_item;
	int i = 0;
	char* k[kv_num];
	char* v[kv_num];
	char* tb_name[kv_num];

	for (i = 0; i < kv_num; i++)
	{
		kv_item = cJSON_GetArrayItem(kv_list, i);
		cJSON* table_name = cJSON_GetObjectItem(kv_item, "table_name");
		cJSON* key = cJSON_GetObjectItem(kv_item, "key");
		cJSON* value = cJSON_GetObjectItem(kv_item, "value");
		struct swarmkv_table *table = NULL;
		struct swarmkv_value *kv = NULL;
	
		k[i] = key->valuestring;
		v[i] = value->valuestring;
		tb_name[i] = table_name->valuestring;

		int slot_owner_id = caculate_key_slot_owner(k[i], store);
		//Am I the owner of the slot?
		assert(slot_owner_id == store->my_node_id);

		HASH_FIND_STR(store->table_hash, tb_name[i], table);
		if(table == NULL)
		{
			//add the table in my swarmkv_store
			table = ALLOC(struct swarmkv_table, 1);
			memcpy(table->table_name, tb_name[i], strlen(tb_name[i]));
			HASH_ADD_STR(store->table_hash, table_name, table);
			//add the key
			kv = ALLOC(struct swarmkv_value, 1);
			kv->key = k[i];
			HASH_ADD_STR(table->owned_value_hash, key, kv);
		}
		HASH_FIND_STR(table->owned_value_hash, k[i], kv);
		if(kv == NULL)
		{
			//add the key
			kv = ALLOC(struct swarmkv_value, 1);
			kv->key = k[i];
			HASH_ADD_STR(table->owned_value_hash, key, kv);
		}
		kv->keylen = strlen(kv->key);
		kv->value = v[i];
		kv->vallen = strlen(kv->value);
		//send invalidate msg to nodes_have_key_cached
		int* cached_node_id;
		struct swarmkv_node* cache_node = NULL;
		for(cached_node_id=(int*)utarray_front(kv->nodes_have_key_cached); cached_node_id!=NULL; cached_node_id=(int*)utarray_next(kv->nodes_have_key_cached,cached_node_id)) 
		{
			HASH_FIND_INT(store->nodes, cached_node_id, cache_node);
			if(cache_node != NULL)
			{
				swarmkv_send_invalidate_keys_req(tb_name[i], k[i], v[i], cache_node, store);
			}
		}
	}
	return 1;
}
*/

typedef void (*swarmkv_cb_function)(char*, char*);

void print_result(char* key, char* value)
{
	printf("get result\n%s: %s\n", key, value);
}

int swarmkv_unpack_msg(char* buf, struct swarmkv_store* store)
{
    cJSON* msg = cJSON_Parse(buf);
	if(!msg) {
    	printf("get msg faild !\n");
		return -1;
	}
	cJSON* msg_type = cJSON_GetObjectItem(msg, "msg_type");
	cJSON* src_id = cJSON_GetObjectItem(msg, "my_id");
	cJSON* src_ip = cJSON_GetObjectItem(msg, "my_ip");
	cJSON* src_port = cJSON_GetObjectItem(msg, "my_port");
	cJSON* tb_name = 0;
	cJSON* key = 0;
	cJSON* value = 0;
	cJSON* cb_function = 0;
	int id = src_id->valueint;

	struct swarmkv_node *src_node = NULL;
	HASH_FIND_INT(store->nodes, &id, src_node); 

	switch(msg_type->valueint)
	{
		case MSG_PING:
		case MSG_PONG:
		case MSG_MEET_RESP:
			//check if the src node is legal
			if(src_node == NULL)
			{
				printf("It is an UNKNOWN node %d!\n", id);
				return -1;
			}

			if(msg_type->valueint == MSG_PING)
				printf("it is a PING msg\n");
			else if((msg_type->valueint == MSG_PONG))
			{
				src_node->pong_received_t = clock();
				printf("it is a PONG msg\n");
			}
			else
			{
				printf("it is a MEET RESPONSE msg\n");
				if(src_node->status == FAIL)
				{
					src_node->status = ON;
					src_node->pfail_cnt = 0;
				}
			}
				
			//check if the msg is complete
			cJSON* node_list = cJSON_GetObjectItem(msg, "node_list");
			if(!node_list)
			{
				printf("no node list!\n");
				return -1;
			}
			cJSON* slot_list = cJSON_GetObjectItem(msg, "slot_list");
			if(!node_list)
			{
				printf("no slot list!\n");
				return -1;
			}
	
			//set the status of src_node to ON
			if(src_node->status == PFAIL)
			{
				src_node->status = ON;
				src_node->pfail_cnt = 0;
			}

			swarmkv_parse_node_and_slot(node_list, slot_list, store);

			//send response msg
			if(msg_type->valueint == MSG_PING)
			{
				swarmkv_send_health_check_msg(MSG_PONG, src_node, store);
			}
			if(msg_type->valueint == MSG_PONG)
			{
				swarmkv_send_health_check_msg(MSG_PING, src_node, store);
			}
			printf("Press enter to continue...\n");
			getchar();
			break;

		case MSG_MEET_REQ:
			printf("it is a MEET REQUEST msg\n");
			//int id = src_id->valueint;
			struct swarmkv_node *new_node;
    		HASH_FIND_INT(store->nodes, &id, new_node); 
			if(new_node == NULL)
			{
				printf("meet a new node\n");
				new_node = ALLOC(struct swarmkv_node, 1);
				new_node->node_id = id;
				HASH_ADD_INT(store->nodes, node_id, new_node);
			}
			else
			{
				printf("meet a exist failed node\n");
			}
			new_node->ip = src_ip->valuestring;
			new_node->port = src_port->valueint;
			new_node->ping_sent_t = 0;
			new_node->pong_received_t = 0;
			new_node->status = ON;
			new_node->pfail_cnt = 0;

			//send a meet response msg
			swarmkv_send_health_check_msg(MSG_MEET_RESP, new_node, store);
			printf("Press enter to continue...\n");
			getchar();
			break;

		case MSG_FAIL_REQ:
			//check if the src node is legal
			if(src_node == NULL)
			{
				printf("It is an UNKNOWN node %d!\n", id);
				return -1;
			}
			printf("It is a FAIL msg\n");
			//find the fail node
			struct swarmkv_node *fail_node = NULL;
			cJSON* fail_node_id = cJSON_GetObjectItem(msg, "fail_node_id");
			int fail_id = fail_node_id->valueint;
			HASH_FIND_INT(store->nodes, &fail_id, fail_node); 
			if(fail_node != NULL)
			{
				fail_node->status = FAIL;
				fail_node->pfail_cnt = 0;
				//send a fail response msg
				ok_response(MSG_FAIL_RESP, src_node, store);
			}
			break;

		case MSG_FAIL_RESP:
			break;

		case MSG_PUT_REQ:
			if(src_node == NULL)
			{
				printf("It is a PUT REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");

			//put the key-value into my store and send invalidate request msg
			swarmkv_put_one_kv(tb_name->valuestring, key->valuestring, value->valuestring, store);
			//send a put response "ok"
			ok_response(MSG_PUT_RESP, src_node, store);
			break;

		case MSG_PUT_RESP:
			break;
		
		case MSG_INVALIDATE_KEYS_REQ:
			//update the cached_value_hash
			if(src_node == NULL)
			{
				printf("It is a INVALIDATE KEYS msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");
	
			struct swarmkv_table* table = NULL;
			struct swarmkv_value* cache_kv = NULL;
			HASH_FIND_STR(store->table_hash, tb_name->valuestring, table);
			if(table != NULL)
			{
				HASH_FIND_STR(table->cached_value_hash, key->valuestring, cache_kv);
				if(cache_kv != NULL)
				{
					cache_kv->value = value->valuestring;
					cache_kv->vallen = strlen(cache_kv->value);
					//send a invalidate key response "ok"
					ok_response(MSG_INVALIDATE_KEYS_RESP, src_node, store);
				}
			}
			break;
		case MSG_INVALIDATE_KEYS_RESP:
			break;
		
		case MSG_GET_REQ:
			if(src_node == NULL)
			{
				printf("It is a GET REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			cb_function = cJSON_GetObjectItem(msg, "callback");
			char* val = NULL;

			val = swarmkv_get_one_kv(tb_name->valuestring, key->valuestring, src_node->node_id, store);
			//send a response
			swarmkv_send_get_resp(key->valuestring, val, cb_function->valuestring, src_node, store);
			break;

		case MSG_GET_RESP:
			if(src_node == NULL)
			{
				printf("It is a GET REQUEST msg from an UNKNOWN node %d!\n", id);
				return -1;
			}
			tb_name = cJSON_GetObjectItem(msg, "table_name");
			key = cJSON_GetObjectItem(msg, "key");
			value = cJSON_GetObjectItem(msg, "value");
			cb_function = cJSON_GetObjectItem(msg, "callback");

			//update my cache
			struct swarmkv_table *tb = NULL;
			struct swarmkv_value *kv = NULL;
			pthread_rwlock_wrlock(&store->rwlock);
			HASH_FIND_STR(store->table_hash, tb_name->valuestring, tb);
			if(tb == NULL)
			{
				//add a new table in my swarmkv_store
				tb = ALLOC(struct swarmkv_table, 1);
				memcpy(tb->table_name, tb_name->valuestring, strlen(tb_name->valuestring));
				HASH_ADD_STR(store->table_hash, table_name, tb);
				//add a new key
				kv = ALLOC(struct swarmkv_value, 1);
				kv->key = key->valuestring;
				HASH_ADD_STR(tb->cached_value_hash, key, kv);
			}
			HASH_FIND_STR(tb->cached_value_hash, key->valuestring, kv);
			if(kv == NULL)
			{
				//add a new key
				kv = ALLOC(struct swarmkv_value, 1);
				kv->key = key->valuestring;
				HASH_ADD_STR(tb->cached_value_hash, key, kv);
			}
			kv->keylen = strlen(kv->key);
			kv->value = value->valuestring;
			kv->vallen = strlen(kv->value);
			//callback
			/*
			swarmkv_cb_function cb_f = NULL;
			cb_f = (swarmkv_cb_function)cb_function->valuestring;
			if(cb_f!=NULL)
			{
				(*cb_f)(key->valuestring, value->valuestring);
			}
			*/
			break;
		default:
			printf("UNKNOWN msg\n");
	}

	return 1;
}


//check cluster nodes status
void swarmkv_node_health_check(int sockfd, struct swarmkv_store *store)
{
	int i = 0;
	int nodes_cnt = HASH_COUNT(store->nodes);
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

/*
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
*/

int add_bootstrap_node(struct swarmkv_store *store)
{
	int node_id = -1;
	//bootstrap node 0
	struct swarmkv_node* bootstrap_node0 = NULL;
	node_id = BOOTSTRAPNODE_ID_0;
	HASH_FIND_INT(store->nodes, &node_id, bootstrap_node0);
	if(bootstrap_node0 == NULL)
	{
		bootstrap_node0 = ALLOC(struct swarmkv_node, 1);
		bootstrap_node0->node_id = BOOTSTRAPNODE_ID_0;
		HASH_ADD_INT(store->nodes, node_id, bootstrap_node0); 
		bootstrap_node0->ip = BOOTSTRAPNODE_IP_0;
		bootstrap_node0->port = BOOTSTRAPNODE_PORT_0;
		bootstrap_node0->ping_sent_t = 0;
		bootstrap_node0->pong_received_t = 0;
		bootstrap_node0->status = FAIL;
		bootstrap_node0->pfail_cnt = 0;
	}
	//bootstrap node 1
	struct swarmkv_node* bootstrap_node1 = NULL;
	node_id = BOOTSTRAPNODE_ID_1;
	HASH_FIND_INT(store->nodes, &node_id, bootstrap_node1);
	if(bootstrap_node1 == NULL)
	{
		bootstrap_node1 = ALLOC(struct swarmkv_node, 1);
		bootstrap_node1->node_id = BOOTSTRAPNODE_ID_1;
		HASH_ADD_INT(store->nodes, node_id, bootstrap_node1); 
		bootstrap_node1->ip = BOOTSTRAPNODE_IP_1;
		bootstrap_node1->port = BOOTSTRAPNODE_PORT_1;
		bootstrap_node1->ping_sent_t = 0;
		bootstrap_node1->pong_received_t = 0;
		bootstrap_node1->status = FAIL;
		bootstrap_node1->pfail_cnt = 0;
	}
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

	//init rwlock
	pthread_rwlock_init(&store->rwlock, NULL);

	//create socket
	struct sockaddr_in peer_addr;
    socklen_t peerlen = sizeof(struct sockaddr_in);
	char recvbuf[BUFSIZ];
	store->sockfd = create_socket(store->my_port);
	printf("store->sockfd : %d\n", store->sockfd);

	//add myselt to store->nodes
	struct swarmkv_node* myself = NULL;
	myself = ALLOC(struct swarmkv_node, 1);
	myself->node_id = store->my_node_id;
	myself->ip = store->my_ip;
	myself->port = store->my_port;
	myself->ping_sent_t = 0;
	myself->pong_received_t = 0;
	myself->status = ON;
	myself->pfail_cnt = 0;
	HASH_ADD_INT(store->nodes, node_id, myself); 

	//add bootstrap nodes
	add_bootstrap_node(store);

	//send meet msg to bootstrap nodes
	struct swarmkv_node* bootstrap_node = NULL;
	for (bootstrap_node = store->nodes; bootstrap_node != NULL; bootstrap_node = (struct swarmkv_node*)(bootstrap_node->hh.next)) 
	{
		if(bootstrap_node->node_id != store->my_node_id)
		{
			swarmkv_send_meet_req_msg(bootstrap_node, store);
		}
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
			//msg_cnt++;
			//printf("recieve %d msg\n", msg_cnt);
			printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
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
				//do not send ping msg to myself
				if(node->node_id == store->my_node_id)
				{
					continue;
				}
				if(node->status == FAIL)
				{
					continue;
				}
				if(node->pong_received_t == 0)
				{
					//printf("send a ping to node %d\n", node->node_id);
					swarmkv_send_health_check_msg(MSG_PING, node, store);
					continue;
				}
				//if there is a node: current_time - pong_sent_t >timeout/2, send a ping msg immediately
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT/2)
				{
					printf("send a ping to node %d\n", node->node_id);
					swarmkv_send_health_check_msg(MSG_PING, node, store);
					continue;
				}
				//if there is a node: current_time - pong_sent_t >timeout, suspect that the node is failed
				if(cur_t - node->pong_received_t > SWARMKV_DEFAULT_NODE_TIMEOUT)
				{
					printf("set a node %d pfail\n", node->node_id);
					node->status = PFAIL;
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
			ret_buff = ALLOC(char, result->vallen);
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