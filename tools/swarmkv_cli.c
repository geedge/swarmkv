#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "uthash.h"
#include "sds.h"
#include "cJSON.h"
#include "swarmkv_error.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "linenoise/linenoise.h"
#include "swarmkv/swarmkv.h"

extern struct swarmkv_reply *swarmkv_cmd_argv(struct swarmkv *db, size_t argc, sds *argv);
struct cluster_manager_command
{
	char* cluster_name;
	int argc;
	char ** argv;
};
struct config
{	
	char db_name[256];
	int consul_port;
	char consul_host[64];
	struct swarmkv *db;
	struct cluster_manager_command cluster_mgr_cmd;
};

struct config g_config;
#define	CLUSTER_MANAGER_MODE() (g_config.cluster_mgr_cmd.cluster_name != NULL)
#define SWARMKV_CLI_VERSION	"3.0.0 Islamabad F7"

static void usage(void)
{
	fprintf(stderr,
	"swarmkv-cli %s\n"
	"\n"
	"Usage: swarmkv-cli [OPTIONS] [cmd [arg [arg ...]]]\n"
	"  -n <cluster-name>  Cluster name used for orchestration.\n"
	"  -p <consul-port>   Consul Agent port (default: 8500).\n"
	"  --cluster-create	<cluster-name> IP port [IP port]  Create cluster orchestration information in Consul KV.\n"	
	"  --exec [Command]  Execute comand and exit.\n",
	SWARMKV_CLI_VERSION);
	
    exit(1);
}

static int consul_get_cluster_leader_block(const char *db_name, unsigned int consul_agent_port, struct swarmkv_node *leader)
{
	sds resp_body=NULL;
	int resp_code=0;
	char url[SWARMKV_URL_MAX]="";
	snprintf(url, sizeof(url),"/v1/kv/swarmkv/%s/lead?raw=1", db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, g_config.consul_host, consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	json2leader(resp_body, leader);
	sdsfree(resp_body);
	return 0;
}
static int consul_list_health_nodes_block(const char *db_name, unsigned int consul_agent_port, struct swarmkv_node *nodes, size_t *n_nodes)
{
	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;

	snprintf(url, sizeof(url), "/v1/health/service/%s?passing=1", db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, g_config.consul_host, consul_agent_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		return -1;
	}
	json2nodes(resp_body, nodes, n_nodes);
	sdsfree(resp_body);
	return 0;
}

static int consul_kv_init_block(char *cluster_name, char *argv[], size_t argc)
{
	char url[SWARMKV_URL_MAX]="";
	int resp_code_put=0;
	const char *ip=NULL, *port=NULL;
	size_t node_number=argc/2;
	size_t slots_per_node=KEYSPACE_SLOT_NUM/node_number;
	struct key_slot slots[KEYSPACE_SLOT_NUM];
	size_t i=0, nth_node=0;
	int ret=0;
	memset(slots, 0, sizeof(slots));
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		nth_node=MIN(i/slots_per_node, node_number-1);
		ip=argv[nth_node*2];
		port=argv[nth_node*2+1];
		slots[i].slot_id=i;
		strncpy(slots[i].owner.addr.ip_addr, ip, sizeof(slots[i].owner.addr));
		slots[i].owner.addr.p2p_port=atoi(port);
	}
	sds slot_json=NULL;
	slot_json=keyslots2json(slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", cluster_name);
	resp_code_put=http_blocking_request(EVHTTP_REQ_PUT, g_config.consul_host, g_config.consul_port, url, slot_json, NULL);
	if(resp_code_put==200)
	{
		fprintf(stderr, "consul KV init slot table %s.\n", url);
		ret=0;
	}
	else
	{		
		fprintf(stderr, "consul KV init slot table %s failed.\n", url);	
		ret=-1;
	}
	sdsfree(slot_json);
	return ret;
}
struct swarmkv_reply *cluster_create_command(struct swarmkv* db, char *argv[], size_t argc)
{
	int ret=0;
	struct swarmkv_reply *reply=NULL;
	if(argc<3 || (argc-1)%2!=0)
	{
		reply=swarmkv_reply_new_error("invalid arguments number");
		return reply;
	}
	ret=consul_kv_init_block(argv[0], argv+1, argc-1);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error("cluster %s  create failed.", argv[0]);
	}
	else
	{
		reply=swarmkv_reply_new_status("OK");
	}
	return reply;
}

struct swarmkv_reply *cluster_nodes_command(struct swarmkv* db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	int ret=0;
	struct swarmkv_node leader; 	
	struct swarmkv_node nodes[SWARMKV_NODE_MAX];
	size_t n_node=SWARMKV_NODE_MAX;
	ret=consul_get_cluster_leader_block(g_config.db_name, g_config.consul_port, &leader);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error(error_cluster_leader_not_found);
		return reply;
	}
	ret=consul_list_health_nodes_block(g_config.db_name, g_config.consul_port, nodes, &n_node);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error(error_cluster_leader_not_found);
		return reply;
	}
	size_t i=0;
	char uuid_str[37];
	int is_leader;
	reply=swarmkv_reply_new_array(n_node);
	for(i=0; i<n_node; i++)
	{
		uuid_unparse_lower(nodes[i].bin_uuid, uuid_str);
		if(0==swarmkv_node_compare(&leader, nodes+i))
		{
			is_leader=1;
		}
		else
		{
			is_leader=0;
		}
		reply->elements[i]=swarmkv_reply_new_string_fmt("%s %s %u %s", uuid_str,
																nodes[i].addr.ip_addr,
																nodes[i].addr.p2p_port,
																is_leader?"leader":"follower");
	}
	return reply;
}
struct swarmkv_reply *cluster_slots_command(struct swarmkv* db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?raw=1", g_config.db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, g_config.consul_host, g_config.consul_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		reply=swarmkv_reply_new_error("read %s%s failed", g_config.consul_host, url);
		return reply;
	}
	reply=swarmkv_reply_new_string(resp_body, sdslen(resp_body)+1);	
	sdsfree(resp_body);
	return reply;
}


int slot_cmp_by_owner(const void * a, const void *b)
{
	const struct key_slot *sa=(const struct key_slot*)a;
	const struct key_slot *sb=(const struct key_slot*)b;
	int ret=0;
	ret=swarmkv_node_addr_compare(&(sa->owner.addr), &(sb->owner.addr));
	if(ret==0)
	{
		ret=sa->slot_id-sb->slot_id;
	}
	return ret;
}
int slot_cmp_by_id(const void * a, const void *b)
{
	const struct key_slot *sa=(const struct key_slot*)a;
	const struct key_slot *sb=(const struct key_slot*)b;
	
	return sa->slot_id-sb->slot_id;
}


struct swarmkv_reply *cluster_addnode_command(struct swarmkv* db, char *argv[], size_t argc)
{
	char uuid[38], ip[64], port[16], membership[16];
	const char *node_ip=argv[2], *node_port=argv[3];
	struct swarmkv_node node;
	struct swarmkv_reply *slots_reply=NULL, *nodes_reply=NULL, *reply=NULL;
	size_t i=0, j=0, actual_rebalanced_slot_num=0;

    if(argc <= 3)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return reply;
    }


	nodes_reply=cluster_nodes_command(db, NULL, 0);
	struct key_slot *slots=NULL;
	if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
	{
		return nodes_reply;
	}
	int node_is_active=0, node_has_slot=0;
	for(i=0; i<nodes_reply->n_element; i++)
	{
		sscanf(nodes_reply->elements[i]->str, "%s %s %s %s", uuid, ip, port, membership);
		if(0==strcmp(ip, node_ip) && 0==strcmp(port, node_port))
		{
			strcpy(node.addr.ip_addr, ip);
			node.addr.p2p_port=atoi(port);
			uuid_parse(uuid, node.bin_uuid);
			node_is_active=1;
			break;
		}
	}
	if(!node_is_active)
	{
		reply=swarmkv_reply_new_error(error_cluster_addnode_node_is_not_active);
		goto error_out;
	}
	slots_reply=cluster_slots_command(db, NULL, 0);
	if(slots_reply->type!=SWARMKV_REPLY_STRING)
	{
		reply=slots_reply;
		slots_reply=NULL;
		goto error_out;
	}
	slots=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	json2keyslots(slots_reply->str, slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		if(0==swarmkv_node_addr_compare(&(slots[i].owner.addr), &(node.addr)))
		{
			node_has_slot=1;
		}
	}
	if(node_has_slot)
	{
		reply=swarmkv_reply_new_error(error_cluster_addnode_node_has_slot);
		goto error_out;
	}

	
	/* STEP 1 Calculate rebalanced slots. */

	
	qsort(slots, KEYSPACE_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_owner);
	struct key_slot *tmp=slots;
	size_t n_active_node=1;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		if(0!=swarmkv_node_compare(&(tmp->owner), &(slots[i].owner)))
		{
			n_active_node++;
			tmp=slots+i;
		}
	}
	int expected_rebalanced_slot_num=KEYSPACE_SLOT_NUM/(n_active_node+1);
	int expected_rebalanced_slot_num_per_node=expected_rebalanced_slot_num/n_active_node;
	int rebalanced_slot_idx[KEYSPACE_SLOT_NUM]={0};
	tmp=slots;
	for(i=0, j=0, actual_rebalanced_slot_num=0; i<KEYSPACE_SLOT_NUM && actual_rebalanced_slot_num<expected_rebalanced_slot_num; i++)
	{
		//Get max expected_rebalanced_slot_num_per_node slots from each active node
		if(j==expected_rebalanced_slot_num_per_node &&
			0==swarmkv_node_addr_compare(&(tmp->owner.addr), &(slots[i].owner.addr)))

		{
			continue;
		}
		//New active node
		if(0!=swarmkv_node_addr_compare(&(tmp->owner.addr), &(slots[i].owner.addr)))
		{
			j=0;
		}
		j++;
		rebalanced_slot_idx[actual_rebalanced_slot_num]=i;
		actual_rebalanced_slot_num++;
		tmp=slots+i;
	}


	/*STEP 2 Do the migration*/
	
	
	struct swarmkv_reply *setslot_reply=NULL, *getkeysinslot_reply=NULL, *addkeystoslot_reply=NULL;
	sds *migrate_argv=NULL;
	printf("%zu slot to be migrated\n", actual_rebalanced_slot_num);
	long long migrated_keys=0;
	for(i=0; i<actual_rebalanced_slot_num; i++)
	{
		tmp=slots+rebalanced_slot_idx[i];
/*		printf("Migrating slot %d from %s %u to %s %u ... ", tmp->slot_id, tmp->owner.addr.ip_addr, tmp->owner.addr.p2p_port,
														node.addr.ip_addr, node.addr.p2p_port);
*/
		/*STEP 2.1 Set NEW node's slot to IMPORTING state. Slot will return to STABLE state after udpate the global slot table*/
														
		setslot_reply=swarmkv_command(db, "keyspace setslot %s %u %d IMPORTING %s %u", node.addr.ip_addr, node.addr.p2p_port,
													tmp->slot_id, tmp->owner.addr.ip_addr, tmp->owner.addr.p2p_port);
		if(setslot_reply->type!=SWARMKV_REPLY_STATUS)
		{
			reply=setslot_reply;
			goto error_out;
		}
		swarmkv_reply_free(setslot_reply);

		/*STEP 2.2 Set OLD node's slot to MIGRATING state*/

		setslot_reply=swarmkv_command(db, "keyspace setslot %s %u %d MIGRATING %s %u", tmp->owner.addr.ip_addr, tmp->owner.addr.p2p_port, 
													tmp->slot_id, node.addr.ip_addr, node.addr.p2p_port);
		if(setslot_reply->type!=SWARMKV_REPLY_STATUS)
		{
			reply=setslot_reply;
			setslot_reply=NULL;
			goto error_out;
		}
		swarmkv_reply_free(setslot_reply);
		setslot_reply=NULL;

		/*STEP 2.3 Get keys from OLD node's slot*/
		
		getkeysinslot_reply=swarmkv_command(db, "keyspace getkeysinslot %s %u %d", tmp->owner.addr.ip_addr, tmp->owner.addr.p2p_port, tmp->slot_id);
		if(getkeysinslot_reply->type!=SWARMKV_REPLY_STRING)
		{
			reply=getkeysinslot_reply;
			getkeysinslot_reply=NULL;
			goto error_out;
		}
		
		/*STEP 2.4 Add those keys to NEW node's slot*/
		
		migrate_argv=ALLOC(sds, 6);
		migrate_argv[0]=sdsnew("keyspace");
		migrate_argv[1]=sdsnew("addkeystoslot");
		migrate_argv[2]=sdsnew(node.addr.ip_addr);
		migrate_argv[3]=sdsfromlonglong(node.addr.p2p_port);
		migrate_argv[4]=sdsfromlonglong(tmp->slot_id);
		migrate_argv[5]=sdsnewlen(getkeysinslot_reply->str, getkeysinslot_reply->len);	
		swarmkv_reply_free(getkeysinslot_reply);
		getkeysinslot_reply=NULL;
		
		addkeystoslot_reply=swarmkv_cmd_argv(db, 6, migrate_argv);
		
		for(j=0; j<6; j++)
		{
			sdsfree(migrate_argv[j]);
			migrate_argv[j]=NULL;
		}
		free(migrate_argv);
		migrate_argv=NULL;
		
		if(addkeystoslot_reply->type!=SWARMKV_REPLY_INTEGER)
		{
			reply=addkeystoslot_reply;
			goto error_out;
		}
		migrated_keys+=addkeystoslot_reply->integer;
//		printf("%lld keys migrated\n", addkeystoslot_reply->integer);
		swarmkv_reply_free(addkeystoslot_reply);
		addkeystoslot_reply=NULL;
		
		
		/*STEP 2.5 Delete keys from OLD node's slot*/
	
		reply=swarmkv_command(db, "keyspace delslotkeys %s %u %d", tmp->owner.addr.ip_addr, tmp->owner.addr.p2p_port, tmp->slot_id);
		if(reply->type!=SWARMKV_REPLY_INTEGER)
		{
			goto error_out;
		}

		swarmkv_node_copy(&(tmp->owner), &node);
		
	}

	
	/*STEP 3 Update global slot table*/

	char url[SWARMKV_URL_MAX]="";
	int resp_code_put=0;
	qsort(slots, KEYSPACE_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_id);
	sds slot_json=NULL;
	slot_json=keyslots2json(slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);

	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", g_config.db_name);
	
	resp_code_put=http_blocking_request(EVHTTP_REQ_PUT, g_config.consul_host, g_config.consul_port, url, slot_json, NULL);
	if(resp_code_put!=200)
	{
		reply=swarmkv_reply_new_error("update slot table failed: %s (%d)", url, resp_code_put);
	}
	else
	{
		reply=swarmkv_reply_new_status("slot table updated, %lld slots and %lld keys were migrated to %s %u",
									actual_rebalanced_slot_num, migrated_keys,
									node.addr.ip_addr, node.addr.p2p_port);
	}
	
error_out:

	if(nodes_reply)	swarmkv_reply_free(nodes_reply);
	if(slots_reply) swarmkv_reply_free(slots_reply);
	if(slots) free(slots);
	return reply;
}

struct swarmkv_reply *cluster_keyslot_command(struct swarmkv* db, char *argv[], size_t argc)
{
	/*KEYSPACE KEYSLOT key*/
	struct swarmkv_reply *reply=NULL;
    if(argc <= 2)
    {
        reply = swarmkv_reply_new_error(error_read_slot_table_failed);
        return reply;
    }

	char* key=argv[2];
	int slot_id=get_slot_id(key, strlen(key));
	reply=swarmkv_reply_new_integer(slot_id);
	return reply;
}
struct swarmkv_reply *cluster_keys_command(struct swarmkv* db, char *argv[], size_t argc)
{
	struct swarmkv_reply *nodes_reply=NULL, *keyspace_keys_reply=NULL, *reply=NULL;
	char uuid[38], ip[64], port[16], membership[16];
	size_t i=0;
    if(argc <= 2)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return reply;
    }
	nodes_reply=cluster_nodes_command(db, NULL, 0);
	if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
	{
		return nodes_reply;
	}
	for(i=0; i<nodes_reply->n_element; i++)
	{
		sscanf(nodes_reply->elements[i]->str, "%s %s %s %s", uuid, ip, port, membership);
		keyspace_keys_reply=swarmkv_command(db, "keyspace keys %s %s %s", ip, port, argv[2]);
		swarmkv_reply_merge_array(&reply, keyspace_keys_reply);
		keyspace_keys_reply=NULL;
	}
	swarmkv_reply_free(nodes_reply);
	if(!reply)
	{
		reply=swarmkv_reply_new_nil();
	}
	return reply;
}

struct replica_node_addr
{
    struct swarmkv_node_addr addr;
	UT_hash_handle hh;
};

struct replica_list
{
    int len;
    sds key;
	struct replica_node_addr *replica_node_hash;
	UT_hash_handle hh;
};

struct cluster_sanity_cmd_ctx
{
    int reply_cnt;
    int heal_replica_cnt;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
	struct replica_list *keyspace_replica_hash;
    struct replica_list *crdt_replica_hash;
};

struct keyspace_cmd_ctx
{
    sds key;
    size_t keylen;
    struct cluster_sanity_cmd_ctx *ctx;
};

void cluster_sanity_cond_signal(void * arg)
{
	struct cluster_sanity_cmd_ctx *ctx=(struct cluster_sanity_cmd_ctx*) arg;
	pthread_cond_signal(&ctx->cond);
	return;
}

int cluster_sanity_cond_timedwait(struct cluster_sanity_cmd_ctx *ctx)
{
	struct timespec max_wait = {0, 0};
	clock_gettime(CLOCK_REALTIME, &max_wait);
	max_wait.tv_sec+=1800;
	int timed_wait_rv=0;

    if(ctx->reply_cnt==0)
	{
        return 0;
	}
    pthread_mutex_lock(&ctx->mutex);
    timed_wait_rv=pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &max_wait);
    pthread_mutex_unlock(&ctx->mutex);

    return timed_wait_rv;
}

void exec_cmd_generic_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
   struct cluster_sanity_cmd_ctx *ctx=(struct cluster_sanity_cmd_ctx *)cb_arg;

    ctx->reply_cnt--;

    if(ctx->reply_cnt==0)
    {
        cluster_sanity_cond_signal(ctx);
    }
    return;
}

void exec_cmd_crdt_connect(struct swarmkv* db, struct cluster_sanity_cmd_ctx *ctx, struct replica_list *keyspace_list, struct replica_node_addr *crdt_addr, char *key)
{
    struct replica_node_addr *tmp=NULL, *keyspace_addr=NULL;

    HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_addr, tmp)
    {
        ctx->reply_cnt++;
        swarmkv_exec_async(db, ctx, exec_cmd_generic_callback, "ctdt connnect %s %s %d %s %d", key, crdt_addr->addr.ip_addr,
                             crdt_addr->addr.p2p_port, keyspace_addr->addr.ip_addr, keyspace_addr->addr.p2p_port);
    }
}

void build_crdt_replica_list(const struct swarmkv_reply* reply, struct cluster_sanity_cmd_ctx *ctx, char *ip, char *port)
{
    int i=0;
    for(i=0; i<reply->n_element; i++)
    {
        struct swarmkv_node_addr node_addr;
        memset(&node_addr, 0, sizeof(struct swarmkv_node_addr));
	    strncpy(node_addr.ip_addr, ip, strlen(ip));
	    node_addr.p2p_port=(unsigned int)atoll(port);

        struct replica_list *crdt_replica_list=NULL;
        HASH_FIND(hh, ctx->crdt_replica_hash, reply->elements[i]->str, reply->elements[i]->len, crdt_replica_list);
        if(!crdt_replica_list)
        {
            crdt_replica_list=ALLOC(struct replica_list, 1);
            crdt_replica_list->key=sdsnewlen(reply->elements[i]->str, reply->elements[i]->len);
            crdt_replica_list->len=reply->elements[i]->len;
            HASH_ADD_KEYPTR(hh, ctx->crdt_replica_hash, crdt_replica_list->key, crdt_replica_list->len, crdt_replica_list);
        }

        struct replica_node_addr *replica_node=NULL;
        replica_node=ALLOC(struct replica_node_addr, 1);
        swarmkv_node_addr_copy(&replica_node->addr, &node_addr);
        HASH_ADD(hh, crdt_replica_list->replica_node_hash, addr, sizeof(struct swarmkv_node_addr), replica_node);
    }
    return;
}

void build_keyspace_replica_list(const struct swarmkv_reply* reply, void * cb_arg)
{
    int i=0;
    struct keyspace_cmd_ctx *cmd_ctx=(struct keyspace_cmd_ctx *)cb_arg;
    for(i=0; i<reply->n_element; i++)
    {
        struct swarmkv_node_addr node_addr;
        swarmkv_reply2node_addr(reply->elements[i], &node_addr);

        struct replica_list *keysapce_replica_list=NULL;
        HASH_FIND(hh, cmd_ctx->ctx->keyspace_replica_hash, cmd_ctx->key, cmd_ctx->keylen, keysapce_replica_list);
        if(!keysapce_replica_list)
        {
            continue;
        }
        struct replica_node_addr *replica_node=NULL;
	    replica_node=ALLOC(struct replica_node_addr, 1);
	    swarmkv_node_addr_copy(&replica_node->addr, &node_addr);
        HASH_ADD(hh, keysapce_replica_list->replica_node_hash, addr, sizeof(struct swarmkv_node_addr), replica_node);
    }

    cmd_ctx->ctx->reply_cnt--;

    if(cmd_ctx->ctx->reply_cnt==0)
    {
        cluster_sanity_cond_signal(cmd_ctx->ctx);
    }

    sdsfree(cmd_ctx->key);
    FREE(&cmd_ctx);

    return;
}

void destroy_relica_list_hash(struct cluster_sanity_cmd_ctx *ctx)
{
    struct replica_list *keyspace_list=NULL, *crdt_list=NULL, *tmp_list=NULL;
    struct replica_node_addr *keyspace_node_addr=NULL, *crdt_node_addr=NULL, *tmp_node_addr=NULL;

    HASH_ITER(hh, ctx->keyspace_replica_hash, keyspace_list, tmp_list)
    {
        HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_node_addr, tmp_node_addr)
        {
            HASH_DEL(keyspace_list->replica_node_hash, keyspace_node_addr);
            FREE(&keyspace_node_addr);
        }
        HASH_DEL(ctx->keyspace_replica_hash, keyspace_list);
        if(keyspace_list->key)
            sdsfree(keyspace_list->key);
        FREE(&keyspace_list);
    }

    HASH_ITER(hh, ctx->crdt_replica_hash, crdt_list, tmp_list)
    {
        HASH_ITER(hh, crdt_list->replica_node_hash, crdt_node_addr, tmp_node_addr)
        {
            HASH_DEL(crdt_list->replica_node_hash, crdt_node_addr);
            FREE(&crdt_node_addr);
        }
        HASH_DEL(ctx->crdt_replica_hash, crdt_list);
        if(crdt_list->key)
            sdsfree(crdt_list->key);
        FREE(&crdt_list);
    }
    FREE(&ctx);
}

struct swarmkv_reply *cluster_sanity_command(struct swarmkv* db, char *argv[], size_t argc)
{
    int i=0, need_fix=0;
    int timed_wait_rv;
    struct swarmkv_reply *keyspace_keys_reply=NULL;
    struct swarmkv_reply *nodes_reply=NULL, *reply=NULL;
    char uuid[38], ip[64], port[16], membership[16];

    if(argc < 3)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg);
        return reply;
    }

    nodes_reply=cluster_nodes_command(db, NULL, 0);
    if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
    {
        return nodes_reply;
    }
    if(0==strcasecmp(argv[2], "heal"))
	{
		need_fix=1;
	}

    struct cluster_sanity_cmd_ctx *ctx=ALLOC(struct cluster_sanity_cmd_ctx, 1);
    pthread_cond_init(&ctx->cond, NULL);
	pthread_mutex_init(&ctx->mutex, NULL);

    //Step1 Build the keyspace replica list
    for(i=0; i<nodes_reply->n_element; i++)
    {
        sscanf(nodes_reply->elements[i]->str, "%s %s %s %s", uuid, ip, port, membership);
        keyspace_keys_reply=swarmkv_command(db, "keyspace keys %s %s *", ip, port);
        swarmkv_reply_merge_array(&reply, keyspace_keys_reply);
        keyspace_keys_reply=NULL;
    }

    for(i=0; reply && i<reply->n_element; i++)
    {
        struct replica_list *keysapce_replica_list=NULL;
        HASH_FIND(hh, ctx->keyspace_replica_hash, reply->elements[i]->str, reply->elements[i]->len, keysapce_replica_list);
        if(!keysapce_replica_list)
        {
            keysapce_replica_list=ALLOC(struct replica_list, 1);
            keysapce_replica_list->key=sdsnewlen(reply->elements[i]->str, reply->elements[i]->len);
            keysapce_replica_list->len=reply->elements[i]->len;
            HASH_ADD_KEYPTR(hh, ctx->keyspace_replica_hash, keysapce_replica_list->key, keysapce_replica_list->len, keysapce_replica_list);
        }

        struct keyspace_cmd_ctx *cmd_ctx=ALLOC(struct keyspace_cmd_ctx, 1);
        cmd_ctx->key=sdsnewlen(reply->elements[i]->str, reply->elements[i]->len);
        cmd_ctx->keylen=reply->elements[i]->len;
        ctx->reply_cnt=reply->n_element;
        cmd_ctx->ctx=ctx;
        swarmkv_exec_async(db, cmd_ctx, build_keyspace_replica_list, "keyspace rlist %s", reply->elements[i]->str);
    }

    if(reply)
    {
        swarmkv_reply_free(reply);
    }

    //Step2 Build the crdt replica list
    for(i=0; i<nodes_reply->n_element; i++)
    {
        sscanf(nodes_reply->elements[i]->str, "%s %s %s %s", uuid, ip, port, membership);
        reply=swarmkv_command(db, "crdt keys %s %s *", ip, port);
        build_crdt_replica_list(reply, ctx, ip, port);
        swarmkv_reply_free(reply);
        reply=NULL;
    }
    swarmkv_reply_free(nodes_reply);

    timed_wait_rv=cluster_sanity_cond_timedwait(ctx);
    if(timed_wait_rv)
    {
        printf("%s\n", swarmkv_util_pthread_cond_timedwait_error_to_string(timed_wait_rv));
        reply=swarmkv_reply_new_error("timeout");
        goto finish;
    }

    //Step3 check/heal by keyspace list
    struct swarmkv_reply *r=NULL;
    struct replica_list *keyspace_list=NULL, *crdt_list=NULL, *tmp_list=NULL;
    struct replica_node_addr *keyspace_node_addr=NULL, *crdt_node_addr=NULL, *tmp_node_addr=NULL;

    HASH_ITER(hh, ctx->keyspace_replica_hash, keyspace_list, tmp_list)
    {
        HASH_FIND(hh, ctx->crdt_replica_hash, keyspace_list->key, sdslen(keyspace_list->key), crdt_list);
        if(!crdt_list && !need_fix)
        {
            r=swarmkv_reply_new_string(keyspace_list->key, sdslen(keyspace_list->key));
            swarmkv_reply_append_string(&reply, r);
        }
        if(!crdt_list && need_fix)
        {
            HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_node_addr, tmp_node_addr)
            {
                ctx->reply_cnt++;
                ctx->heal_replica_cnt++;
                swarmkv_exec_async(db, ctx, exec_cmd_generic_callback, "keyspace rdel %s %d %s", keyspace_node_addr->addr.ip_addr, keyspace_node_addr->addr.p2p_port, keyspace_list->key);
            }
        }

        if(crdt_list)
        {
            //Step3.1 foreach keysapce list, if keysapce node not in crdt list, keyspace rdel node
            HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_node_addr, tmp_node_addr)
            {
                HASH_FIND(hh, crdt_list->replica_node_hash, &keyspace_node_addr->addr, sizeof(struct swarmkv_node_addr), crdt_node_addr);
                if(!crdt_node_addr && !need_fix)
                {
                    r=swarmkv_reply_new_string(keyspace_list->key, sdslen(keyspace_list->key));
                    swarmkv_reply_append_string(&reply, r);
                }
                if(!crdt_node_addr && need_fix)
                {
                    ctx->reply_cnt++;
                    ctx->heal_replica_cnt++;
                    swarmkv_exec_async(db, ctx, exec_cmd_generic_callback, "keyspace rdel %s %d %s", keyspace_node_addr->addr.ip_addr, keyspace_node_addr->addr.p2p_port, keyspace_list->key);
                }
            }
            //Step3.2 foreach crdt list, if crdt node not in keyspace list, keyspace radd node
            HASH_ITER(hh, crdt_list->replica_node_hash, crdt_node_addr, tmp_node_addr)
            {
                HASH_FIND(hh, keyspace_list->replica_node_hash, &crdt_node_addr->addr, sizeof(struct swarmkv_node_addr), keyspace_node_addr);
                if(!keyspace_node_addr && !need_fix)
                {
                    r=swarmkv_reply_new_string(keyspace_list->key, sdslen(keyspace_list->key));
                    swarmkv_reply_append_string(&reply, r);
                }
                if(!keyspace_node_addr && need_fix)
                {
                    ctx->reply_cnt++;
                    ctx->heal_replica_cnt++;
                    swarmkv_exec_async(db, ctx, exec_cmd_generic_callback, "keyspace radd %s %s %d",crdt_list->key, crdt_node_addr->addr.ip_addr, crdt_node_addr->addr.p2p_port);
                    exec_cmd_crdt_connect(db,ctx, keyspace_list, crdt_node_addr, crdt_list->key);
                }
            }
        }
    }

    //Step4 check/heal by crdt list
    HASH_ITER(hh, ctx->crdt_replica_hash, crdt_list, tmp_list)
    {
        HASH_FIND(hh, ctx->keyspace_replica_hash, crdt_list->key, sdslen(crdt_list->key), keyspace_list);
        if(!keyspace_list && !need_fix)
        {
            r=swarmkv_reply_new_string(crdt_list->key, sdslen(crdt_list->key));
            swarmkv_reply_append_string(&reply, r);
        }
        if(!keyspace_list && need_fix)
        {
            HASH_ITER(hh, crdt_list->replica_node_hash, crdt_node_addr, tmp_node_addr)
            {
                ctx->reply_cnt++;
                ctx->heal_replica_cnt++;
                swarmkv_exec_async(db, ctx, exec_cmd_generic_callback, "keyspace radd %s %s %d", crdt_list->key, crdt_node_addr->addr.ip_addr, crdt_node_addr->addr.p2p_port);
            }
        }
    }

    timed_wait_rv=cluster_sanity_cond_timedwait(ctx);
    if(timed_wait_rv)
    {
        printf("%s\n", swarmkv_util_pthread_cond_timedwait_error_to_string(timed_wait_rv));
        reply=swarmkv_reply_new_error("timeout");
        goto finish;
    }

    if(need_fix)
    {
        reply=swarmkv_reply_new_integer(ctx->heal_replica_cnt);
        goto finish;
    }

    if(!reply)
    {
        reply=swarmkv_reply_new_integer(0);
    }
finish:
    destroy_relica_list_hash(ctx);
	return reply;
}

typedef struct swarmkv_reply *cluster_command_func(struct swarmkv* db, char *argv[], size_t argc);

struct cluster_cmd_spec
{
	const char *name;
	const char *hint;
	cluster_command_func *func;
};
struct cluster_cmd_spec cluster_cmds[]={
	{"CLUSTER KEYS", "pattern", cluster_keys_command},
	{"CLUSTER KEYSLOT", "key", cluster_keyslot_command},
	{"CLUSTER NODES", "", cluster_nodes_command},
	{"CLUSTER SLOTS", "", cluster_slots_command},
	{"CLUSTER ADDNODE", "IP port", cluster_addnode_command},
	{"CLUSTER CREATE", "cluster-name IP port [IP port]", cluster_create_command},
    {"CLUSTER SANITY", "check | heal", cluster_sanity_command}
};
int is_cluster_command(int argc, char *argv[])
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0;
	char line[256];
	snprintf(line, sizeof(line), "%s %s", argv[0], argv[1]);
	for(i=0; i<n_cmd; i++)
	{
		if(0==strncasecmp(line, cluster_cmds[i].name, strlen(cluster_cmds[i].name)))
		{
			return 1;
		}
	}
	return 0;
}
size_t get_possible_cluster_command_name(const char *prefix, const char* cmd_names[], size_t sz)
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t n_matched=0;
	size_t i=0;
	for(i=0; i<n_cmd; i++)
	{
		if(0==strncasecmp(cluster_cmds[i].name, prefix, strlen(prefix)) && n_matched<sz)
		{
			cmd_names[n_matched]=cluster_cmds[i].name;
			n_matched++;
		}
	}
	return n_matched;
}
const char *get_cluster_command_hint(const char* cmd_name)
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0;
	for(i=0; i<n_cmd; i++)
	{
		if(0==strcasecmp(cluster_cmds[i].name, cmd_name))
		{
			return cluster_cmds[i].hint;
		}
	}
	return NULL;
}

cluster_command_func *get_cluster_func(const char* line)
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0, max_match_len=0;
	cluster_command_func *ret_func=NULL;
	for(i=0; i<n_cmd; i++)
	{
		if(0==strncasecmp(line, cluster_cmds[i].name, strlen(cluster_cmds[i].name)))
		{
			//match the longgest command
			//make sure that command "CLUSTER KEYSLOT" does not match "CLUSTER KEYS"
			if(max_match_len<strlen(cluster_cmds[i].name))
			{
				ret_func=cluster_cmds[i].func;
				max_match_len=strlen(cluster_cmds[i].name);
			}
		}
	}
	return ret_func;
}
struct swarmkv_reply *cluster_command_argv(struct swarmkv *db, size_t argc, sds *argv)
{
	char line[256];
	snprintf(line, sizeof(line), "%s %s", argv[0], argv[1]);
	cluster_command_func *cmd_func=get_cluster_func(line);
	if(cmd_func)
	{
		return cmd_func(db, argv, argc);
	}
	else
	{
		return NULL;
	}
}

struct swarmkv_reply *cluster_command(struct swarmkv *db, const char *line)
{
	int argc=0;
	sds *argv=NULL;
	argv=sdssplitargs(line, &argc);

	struct swarmkv_reply *reply=cluster_command_argv(db, argc, argv);

    if (argv)
        sdsfreesplitres(argv, argc);

	return reply;
}

void completion(const char *buff, linenoiseCompletions *lc)
{
	size_t n_matched=0, i=0;
	const char *possible_cmd[32];
	n_matched=swarmkv_get_possible_command_name(g_config.db, buff, possible_cmd, 32);
	n_matched+=get_possible_cluster_command_name(buff, possible_cmd+n_matched, 32-n_matched);
	for(i=0; i<n_matched; i++)
	{
		linenoiseAddCompletion(lc, possible_cmd[i]);
	}
}

char *hints(const char *cmd, int *color, int *bold)
{
	const char *p=NULL;
	static char buff[512];
	p=swarmkv_get_command_hint(g_config.db, cmd);
	if(!p)
		p=get_cluster_command_hint(cmd);

	if(p)
	{
		*color = 90;
		*bold = 0;
		snprintf(buff, sizeof(buff), " %s", p);
		return buff;
	}
	
	return NULL;
}

void print_reply(const struct swarmkv_reply *reply)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_INTEGER:
			printf("(integer) %lld\n", reply->integer);
			break;
		case SWARMKV_REPLY_NIL:
			printf("(nil)\n");
			break;
		case SWARMKV_REPLY_ERROR:
			printf("(error) %s\n", reply->str);
			break;
		case SWARMKV_REPLY_NODE:
		case SWARMKV_REPLY_STATUS:
			printf("%s\n", reply->str);
			break;
		case SWARMKV_REPLY_STRING:
			printf("\"%.*s\"\n", reply->len, reply->str);
			break;
		case SWARMKV_REPLY_ARRAY:
			for(i=0; i<reply->n_element; i++)
			{
				printf("%zu) ", i);
				print_reply(reply->elements[i]);
			}
			break;
		default:
			break;

	}
}
int main(int argc, char * argv[])
{
	const char *history_file="swarmkv-cli-history.txt";
	char *line;	
	struct swarmkv_reply *reply=NULL;
	memset(&g_config, 0, sizeof(g_config));
	g_config.consul_port=8500;
	strcpy(g_config.consul_host, "127.0.0.1");
    int i;
	sds *exec_argv;
	int exec_argc=0;
    for (i = 1; i < argc; i++)
	{
        int lastarg = i==argc-1;
		if (!strcmp(argv[i], "-n") && !lastarg)
		{
			strncpy(g_config.db_name, argv[++i], sizeof(g_config.db_name));
		}
		else if(!strcmp(argv[i], "-p") && !lastarg)
		{
			sscanf(argv[++i], "%u", &g_config.consul_port);
		}
		else if(!strcmp(argv[i], "--cluster-create") && !lastarg)
		{
			g_config.cluster_mgr_cmd.cluster_name=argv[++i];
			int j = i;
			while (j < argc && argv[j][0] != '-') j++;
			if (j > i) j--;
			reply=cluster_create_command(NULL, argv+i, j-i+1);
			i = j;
		}
		else if(!strcmp(argv[i], "--exec") && !lastarg)
		{
			exec_argv=ALLOC(sds, argc);
			while (++i < argc && argv[i][0] != '-') 
			{
				exec_argv[exec_argc]=sdsnew(argv[i]);				
				exec_argc++;
			}
		}
		else
		{
			usage();
		}
    }
	
	if(CLUSTER_MANAGER_MODE())
	{
		print_reply(reply);
		swarmkv_reply_free(reply);
		return 1;
	}
	
	if(!strlen(g_config.db_name))
	{
		usage();
		return 0;
	}
	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_dryrun(opts);
	swarmkv_options_set_worker_thread_number(opts, 2);
	char *err=NULL;
	g_config.db=swarmkv_open(opts, g_config.db_name, &err);
	if(err)
	{
		fprintf(stderr, "Open DB %s error: %s", g_config.db_name, err);
		free(err);
		free(exec_argv);
		err=NULL;
		return 0;
	}

	//command is part of swarmkv-cli arguments.
	if(exec_argc)
	{
		if(is_cluster_command(exec_argc, exec_argv))
		{
			reply=cluster_command_argv(g_config.db, exec_argc, exec_argv);
		}
		else
		{
			reply=swarmkv_cmd_argv(g_config.db, exec_argc, exec_argv);
		}
		print_reply(reply);
		swarmkv_reply_free(reply);
		for(i=0; i<exec_argc; i++) sdsfree(exec_argv[i]);
		free(exec_argv);
		return 1;
	}
	
	/* Set the completion callback. This will be called every time the
	* user uses the <tab> key. */
	linenoiseSetCompletionCallback(completion);
	linenoiseSetHintsCallback(hints);

	linenoiseHistoryLoad(history_file); /* Load the history at startup */

	char prompt[256];
	snprintf(prompt, sizeof(prompt), "%s> ", g_config.db_name);
	while((line = linenoise(prompt)) != NULL)
	{
		if(0==strlen(line))
		{
			continue;
		}
		
		exec_argv=sdssplitargs(line, &exec_argc);
		
		if(is_cluster_command(exec_argc, exec_argv))
		{
			reply=cluster_command_argv(g_config.db, exec_argc, exec_argv);
		}
		else
		{
			reply=swarmkv_cmd_argv(g_config.db, exec_argc, exec_argv);
		}
		print_reply(reply);
		swarmkv_reply_free(reply);
		reply=NULL;
		linenoiseHistoryAdd(line); // Add to the history. 
		linenoiseHistorySave(history_file); // Save the history on disk. 
		
		free(line);
		
	    if (exec_argv)
	        sdsfreesplitres(exec_argv, exec_argc);
	}
	return 0;

}



