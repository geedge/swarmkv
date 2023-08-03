#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>

#include "uthash.h"
#include "sds.h"
#include "cJSON.h"
#include "swarmkv_error.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "linenoise/linenoise.h"
#include "swarmkv/swarmkv.h"

struct swarmkv_reply *swarmkv_command_on_argv(struct swarmkv *db, const char *target, int argc, sds *argv);

struct cluster_manager_command
{
	char* cluster_name;
	int argc;
	char ** argv;
};
struct config
{	
	char db_name[256];
	unsigned int consul_port;
	unsigned int cluster_port;
	char consul_host[64];
	struct swarmkv *db;
	struct cluster_manager_command cluster_mgr_cmd;
};
struct runtime
{
	char prompt[512];
	int is_attaching;
	char target[128];
};

struct config g_config;
struct runtime g_runtime;
#define	CLUSTER_MANAGER_MODE() (g_config.cluster_mgr_cmd.cluster_name != NULL)
#define SWARMKV_CLI_VERSION	"3.0.0 Islamabad F7"

static void usage(void)
{
	fprintf(stderr,
	"swarmkv-cli %s\n"
	"\n"
	"Usage: swarmkv-cli [OPTIONS] [command [arg [arg ...]]]\n"
	"  -n <cluster-name>  Cluster name used for orchestration.\n"
	"  -c <consul-ip:consul-port>       Consul Agent ip and port (port default: 8500).\n"
	"  -l <p2p-port>      Listening port for Peer-to-Peer networking (default: random).\n"
	"  --cluster-create <cluster-name> IP:port [IP:port ...]  Create cluster orchestration information in Consul KV.\n"
	"  --exec [Command]  Execute comand and exit.\n"
    "  --attach <IP:port>  Attach to a node.\n",
	SWARMKV_CLI_VERSION);
    exit(1);
}

const char* get_attach_target(void)
{
    return g_runtime.is_attaching ? g_runtime.target : NULL;
}

static int consul_get_cluster_leader_block(const char *db_name, unsigned int consul_agent_port, node_t *leader)
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
	leader_response2leader_node(resp_body, leader);
	sdsfree(resp_body);
	return 0;
}
static int consul_list_health_nodes_block(const char *db_name, unsigned int consul_agent_port, node_t *nodes, uuid_t *uuids, size_t *n_nodes)
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
	health_response2active_nodes(resp_body, nodes, uuids, n_nodes);
	sdsfree(resp_body);
	return 0;
}

static int consul_kv_init_block(char *cluster_name, char *argv[], size_t argc)
{
	char url[SWARMKV_URL_MAX]="";
	int resp_code_put=0;
	size_t node_number=argc;
	size_t slots_per_node=KEYSPACE_SLOT_NUM/node_number;
	struct key_slot slots[KEYSPACE_SLOT_NUM];
	size_t i=0, nth_node=0;
	int ret=0;
	memset(slots, 0, sizeof(slots));

	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		nth_node=MIN(i/slots_per_node, node_number-1);

		slots[i].slot_id=i;
		ret=node_init_from_sds( &(slots[i].owner), argv[nth_node]);
		if(ret!=0)
		{
			fprintf(stderr, "Invalid address %s.\n", argv[nth_node]);	
			return -1;
		}
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
struct swarmkv_reply *cluster_create_command(struct swarmkv *db, char *argv[], size_t argc)
{
	int ret=0;
	struct swarmkv_reply *reply=NULL;
	if(argc<2)
	{
		reply=swarmkv_reply_new_error("ERR invalid arguments number");
		return reply;
	}
	ret=consul_kv_init_block(argv[0], argv+1, argc-1);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error("ERR cluster %s create failed.", argv[0]);
	}
	else
	{
		reply=swarmkv_reply_new_status("OK");
	}
	return reply;
}

struct swarmkv_reply *cluster_nodes_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	int ret=0, no_leader=0;
	node_t leader; 	
	node_t nodes[SWARMKV_NODE_MAX];
	size_t n_node=SWARMKV_NODE_MAX;
	uuid_t uuids[SWARMKV_NODE_MAX];
	ret=consul_get_cluster_leader_block(g_config.db_name, g_config.consul_port, &leader);
	if(ret<0)
	{
		no_leader=1;
	}
	ret=consul_list_health_nodes_block(g_config.db_name, g_config.consul_port, nodes, uuids, &n_node);
	if(ret<0)
	{
		reply=swarmkv_reply_new_error(error_cluster_no_node);
		return reply;
	}
	size_t i=0;
	char uuid_str[37];
	int is_leader;
	int brief=0;
	if(argc>2 && 0==strcasecmp(argv[2], "brief"))
	{
		brief=1;
	}
	reply=swarmkv_reply_new_array(n_node);
	for(i=0; i<n_node; i++)
	{
		if(brief)
		{
			reply->elements[i]=swarmkv_reply_new_string_fmt("%s",
															nodes[i].addr);

		}
		else
		{
			uuid_unparse_lower(uuids[i], uuid_str);
			if(!no_leader && 0==node_compare(&leader, nodes+i))
			{
				is_leader=1;
			}
			else
			{
				is_leader=0;
			}
			reply->elements[i]=swarmkv_reply_new_string_fmt("%s %s %s", uuid_str,
																	nodes[i].addr,
																	is_leader?"leader":"follower");
		}

	}
	return reply;
}
struct swarmkv_reply *cluster_slots_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	char url[SWARMKV_URL_MAX]="";
	sds resp_body=NULL;
	int resp_code=0;
	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots?raw=1", g_config.db_name);
	resp_code=http_blocking_request(EVHTTP_REQ_GET, g_config.consul_host, g_config.consul_port, url, NULL, &resp_body);
	if(resp_code!=200)
	{
		reply=swarmkv_reply_new_error("read %s:%u%s failed", g_config.consul_host, g_config.consul_port, url);
		return reply;
	}
	reply=swarmkv_reply_new_verbatim(resp_body, sdslen(resp_body)+1, "js");	
	sdsfree(resp_body);
	return reply;
}


int slot_cmp_by_owner(const void * a, const void *b)
{
	const struct key_slot *sa=(const struct key_slot*)a;
	const struct key_slot *sb=(const struct key_slot*)b;
	int ret=0;
	ret=node_compare(&sa->owner, &sb->owner);
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

int is_node_active(struct swarmkv_reply *nodes_reply, node_t *node)
{
	node_t tmp;
	int node_is_active=0;
	for(size_t i=0; i<nodes_reply->n_element; i++)
	{
		node_init_from_sds(&tmp, nodes_reply->elements[i]->str);
		if(0==node_compare(node, &tmp))
		{
			node_is_active=1;
			break;
		}
	}
	return node_is_active;
}
struct swarmkv_reply *cluster_addslotowner_command(struct swarmkv *db, char *argv[], size_t argc)
{
	size_t n_new_node=argc-2;
	node_t new_nodes[n_new_node];
	struct swarmkv_reply *slots_reply=NULL, *nodes_reply=NULL, *reply=NULL;
	size_t i=0, j=0, actual_rebalanced_slot_num=0;
	struct key_slot *old_slots=NULL, *new_slots=NULL;
    if(argc <= 2)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg, "CLUSTER ADDSLOTOWNER");
        return reply;
    }
	char *cmd_cluster_nodes[]={"cluster", "nodes", "brief"};
	nodes_reply=cluster_nodes_command(db, cmd_cluster_nodes, 3);
	if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
	{
		return nodes_reply;
	}
	for(i=0; i<n_new_node; i++)
	{
		if(0!=node_init_from_sds(new_nodes+i, argv[2+i]))
		{
			reply=swarmkv_reply_new_error(error_arg_not_valid_address, argv[2+i]);
			goto error_out;
		}
		if(!is_node_active(nodes_reply, new_nodes+i))
		{
			reply=swarmkv_reply_new_error(error_cluster_addslotowner_node_is_not_active, (new_nodes+i)->addr);
			goto error_out;
		}
	}
	
	slots_reply=cluster_slots_command(db, NULL, 0);
	
	if(slots_reply->type!=SWARMKV_REPLY_VERBATIM)
	{
		reply=slots_reply;
		slots_reply=NULL;
		goto error_out;
	}
	old_slots=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	new_slots=ALLOC(struct key_slot, KEYSPACE_SLOT_NUM);
	json2keyslots(slots_reply->str, old_slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);
	for(i=0; i<n_new_node; i++)
	{
		for(j=0; j<KEYSPACE_SLOT_NUM; j++)
		{
			if(0==node_compare(&(old_slots[j].owner), new_nodes+i))
			{
				reply=swarmkv_reply_new_error(error_cluster_addslotowner_node_has_slot, (new_nodes+i)->addr);
				goto error_out;
			}
		}
	}
	
	/* STEP 1 Calculate rebalanced slots. */

	
	qsort(old_slots, KEYSPACE_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_owner);
	struct key_slot *tmp=old_slots;
	size_t n_active_node=1;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		if(0!=node_compare(&(tmp->owner), &(old_slots[i].owner)))
		{
			n_active_node++;
			tmp=old_slots+i;
		}
	}
	int expected_rebalanced_slot_num=KEYSPACE_SLOT_NUM*n_new_node/(n_active_node+n_new_node);
	int expected_rebalanced_slot_num_per_old_node=expected_rebalanced_slot_num/n_active_node;
	int expected_repbalanced_slot_num_per_new_node=expected_rebalanced_slot_num/n_new_node;

	struct key_slot *previous_slot=old_slots;
	size_t n_slot_from_current_old_node=0, n_slot_to_current_new_node=0;
	for(i=0, j=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		new_slots[i].slot_id=old_slots[i].slot_id;
		if(0!=node_compare(&(previous_slot->owner), &(old_slots[i].owner)))
		{
			n_slot_from_current_old_node=0;
		}
		
		if(n_slot_from_current_old_node==expected_rebalanced_slot_num_per_old_node)
		{
			node_copy(&new_slots[i].owner, &old_slots[i].owner);
		}
		else
		{
			if(n_slot_to_current_new_node==expected_repbalanced_slot_num_per_new_node &&
				j<n_new_node-1)
			{
				j++;
				n_slot_to_current_new_node=0;
			}
			node_copy(&new_slots[i].owner, new_nodes+j);
			n_slot_to_current_new_node++;
			n_slot_from_current_old_node++;
			actual_rebalanced_slot_num++;
		}
		previous_slot=old_slots+i;
	}

	/*STEP 2 Do the migration*/
	
	
	struct swarmkv_reply *setslot_reply=NULL, *getkeysinslot_reply=NULL, *addkeystoslot_reply=NULL;
	sds *migrate_argv=NULL;
	printf("%zu slots to be migrated\n", actual_rebalanced_slot_num);
	long long migrated_keys=0;
	node_t *new_node=NULL, *old_node=NULL;
	int slot_id=0;
	for(i=0; i<KEYSPACE_SLOT_NUM; i++)
	{
		new_node=&new_slots[i].owner;
		old_node=&old_slots[i].owner;
		slot_id=new_slots[i].slot_id;
		assert(new_slots[i].slot_id == old_slots[i].slot_id);
		if(0==node_compare(new_node, old_node))
		{
			continue;
		}
/*		printf("Migrating slot %d from %s %u to %s %u ... ", tmp->slot_id, tmp->owner.addr.ip_addr, tmp->owner.addr.cluster_port,
														node.addr.ip_addr, node.addr.cluster_port);
*/
		/*STEP 2.1 Set NEW node's slot to IMPORTING state. Slot will return to STABLE state after udpate the global slot table*/
		setslot_reply=swarmkv_command_on(db, new_node->addr, "keyspace setslot %d IMPORTING %s", 
													slot_id, old_node->addr);
		if(setslot_reply->type!=SWARMKV_REPLY_STATUS)
		{
			reply=setslot_reply;
			goto error_out;
		}
		swarmkv_reply_free(setslot_reply);

		/*STEP 2.2 Set OLD node's slot to MIGRATING state*/

		setslot_reply=swarmkv_command_on(db, old_node->addr, "keyspace setslot %d MIGRATING %s",
													slot_id, new_node->addr);
		if(setslot_reply->type!=SWARMKV_REPLY_STATUS)
		{
			reply=setslot_reply;
			setslot_reply=NULL;
			goto error_out;
		}
		swarmkv_reply_free(setslot_reply);
		setslot_reply=NULL;

		/*STEP 2.3 Get keys from OLD node's slot*/

		getkeysinslot_reply=swarmkv_command_on(db, old_node->addr, "keyspace getkeysinslot %d", slot_id);
		if(getkeysinslot_reply->type!=SWARMKV_REPLY_STRING)
		{
			reply=getkeysinslot_reply;
			getkeysinslot_reply=NULL;
			goto error_out;
		}


		/*STEP 2.4 Add those keys to NEW node's slot*/
		
		migrate_argv=ALLOC(sds, 4);
		migrate_argv[0]=sdsnew("keyspace");
		migrate_argv[1]=sdsnew("addkeystoslot");
		migrate_argv[2]=sdsfromlonglong(slot_id);
		migrate_argv[3]=sdsnewlen(getkeysinslot_reply->str, getkeysinslot_reply->len);	
		swarmkv_reply_free(getkeysinslot_reply);
		getkeysinslot_reply=NULL;

		addkeystoslot_reply=swarmkv_command_on_argv(db, new_node->addr, 4, migrate_argv);

		for(j=0; j<4; j++)
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

		reply=swarmkv_command_on(db, old_node->addr, "keyspace delslotkeys %d", slot_id);
		if(reply->type!=SWARMKV_REPLY_INTEGER)
		{
			goto error_out;
		}
		swarmkv_reply_free(reply);
		reply=NULL;
	}
	//return swarmkv_reply_new_status("OK");

	/*STEP 3 Update global slot table*/

	char url[SWARMKV_URL_MAX]="";
	int resp_code_put=0;
	qsort(new_slots, KEYSPACE_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_id);
	sds slot_json=NULL;
	slot_json=keyslots2json(new_slots, sizeof(struct key_slot), 0, KEYSPACE_SLOT_NUM);

	snprintf(url, sizeof(url), "/v1/kv/swarmkv/%s/slots", g_config.db_name);
	
	resp_code_put=http_blocking_request(EVHTTP_REQ_PUT, g_config.consul_host, g_config.consul_port, url, slot_json, NULL);
	if(resp_code_put!=200)
	{
		reply=swarmkv_reply_new_error("update global slot table failed: %s (%d)", url, resp_code_put);
	}
	else
	{
		reply=swarmkv_reply_new_status("global slot table is updated, %lld slots and %lld keys were migrated to %zu nodes.",
									actual_rebalanced_slot_num, migrated_keys,
									n_new_node);
	}
	sdsfree(slot_json);
error_out:

	if(nodes_reply)	swarmkv_reply_free(nodes_reply);
	if(slots_reply) swarmkv_reply_free(slots_reply);
	if(old_slots) free(old_slots);
	if(new_slots) free(new_slots);
	return reply;
}

struct swarmkv_reply *cluster_keys_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *nodes_reply=NULL, *thread_reply=NULL, *keyspace_keys_reply=NULL, *reply=NULL;
	size_t i=0;
    if(argc <= 2)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg, "CLUSTER KEYS");
        return reply;
    }
	char *cmd_cluster_nodes[]={"cluster", "nodes", "brief"};
	nodes_reply=cluster_nodes_command(db, cmd_cluster_nodes, 3);
	if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
	{
		return nodes_reply;
	}
	for(i=0; i<nodes_reply->n_element; i++)
	{
		thread_reply=swarmkv_command_on(db, nodes_reply->elements[i]->str, "info threads");
		for(int j=0; j<thread_reply->integer; j++)
		{
			keyspace_keys_reply=swarmkv_command_on(db, nodes_reply->elements[i]->str, "keyspace keys %d %s", j, argv[2]);
			swarmkv_reply_merge_array(&reply, keyspace_keys_reply);
			if(keyspace_keys_reply->type==SWARMKV_REPLY_NIL)
			{
				swarmkv_reply_free(keyspace_keys_reply);
			}
			keyspace_keys_reply=NULL;
		}
		swarmkv_reply_free(thread_reply);
	}
	swarmkv_reply_free(nodes_reply);
	if(!reply)
	{
		reply=swarmkv_reply_new_nil();
	}
	return reply;
}
struct swarmkv_reply *cluster_info_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *nodes_reply=NULL, *per_node_info_reply=NULL;
	struct swarmkv_reply *reply;
	char *cmd_cluster_nodes[]={"cluster", "nodes", "brief"};
    nodes_reply=cluster_nodes_command(db, cmd_cluster_nodes, 3);
    if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
    {
        return nodes_reply;
    }
	char *aligned_info=NULL;
	reply=swarmkv_reply_new_array(nodes_reply->n_element);
	for(size_t i=0; i<nodes_reply->n_element; i++)
	{
		if(argc>2)
		{
			per_node_info_reply=swarmkv_command_on(db, nodes_reply->elements[i]->str, "info %s", argv[2]);
		}
		else
		{
			per_node_info_reply=swarmkv_command_on(db, nodes_reply->elements[i]->str, "info");			
		}
		reply->elements[i]=swarmkv_reply_new_array(2);
		reply->elements[i]->elements[0]=swarmkv_reply_new_string_fmt(nodes_reply->elements[i]->str);
		aligned_info=str_replace(per_node_info_reply->str, "\n", "\n      ");
		reply->elements[i]->elements[1]=swarmkv_reply_new_status(aligned_info);
		free(aligned_info);
		swarmkv_reply_free(per_node_info_reply);
	}
	swarmkv_reply_free(nodes_reply);
	if(!reply)
	{
		reply=swarmkv_reply_new_nil();
	}
	return reply;
}
struct replica_node
{
    node_t node;
	UT_hash_handle hh;
};

struct replica_list
{
    int len;
    sds key;
	struct replica_node *replica_node_hash;
	UT_hash_handle hh;
};

struct cluster_sanity_ctx
{
    int reply_num;
    int heal_replica;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
	struct replica_list *keyspace_replica_hash;
    struct replica_list *crdt_replica_hash;
};

struct cluster_sanity_ctx *g_sanity_ctx;

void cluster_sanity_cond_signal(void * arg)
{
	struct cluster_sanity_ctx *ctx=(struct cluster_sanity_ctx*) arg;
	pthread_cond_signal(&ctx->cond);
	return;
}

int cluster_sanity_cond_timedwait(struct cluster_sanity_ctx *ctx)
{
	struct timespec max_wait = {0, 0};
	clock_gettime(CLOCK_REALTIME, &max_wait);
	max_wait.tv_sec+=100;
	int timed_wait_rv=0;

    if(ctx->reply_num==0)
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
    struct cluster_sanity_ctx *ctx=(struct cluster_sanity_ctx *)cb_arg;
    atomic_dec(&ctx->reply_num);
    if(ctx->reply_num<=0)
    {
        cluster_sanity_cond_signal(ctx);
    }
    return;
}

void exec_cmd_crdt_join(struct swarmkv *db, struct cluster_sanity_ctx *ctx, struct replica_list *keyspace_list, struct replica_node *crdt_addr, char *key)
{
    struct replica_node *tmp=NULL, *keyspace_addr=NULL;

    HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_addr, tmp)
    {
        ctx->reply_num++;
        swarmkv_async_command_on(db, exec_cmd_generic_callback, ctx, keyspace_addr->node.addr, "CRDT JOIN %s %s", key, crdt_addr->node.addr);
    }
}

void build_crdt_replica_list(const struct swarmkv_reply* keys_reply, struct cluster_sanity_ctx *ctx, const node_t *node)
{
    int i=0;
    for(i=0; i<keys_reply->n_element; i++)
    {
        struct replica_list *crdt_replica_list=NULL;
        HASH_FIND(hh, ctx->crdt_replica_hash, keys_reply->elements[i]->str, keys_reply->elements[i]->len, crdt_replica_list);
        if(!crdt_replica_list)
        {
            crdt_replica_list=ALLOC(struct replica_list, 1);
            crdt_replica_list->key=sdsnewlen(keys_reply->elements[i]->str, keys_reply->elements[i]->len);
            crdt_replica_list->len=keys_reply->elements[i]->len;
            HASH_ADD_KEYPTR(hh, ctx->crdt_replica_hash, crdt_replica_list->key, crdt_replica_list->len, crdt_replica_list);
        }

        struct replica_node *replica_node=NULL;
        replica_node=ALLOC(struct replica_node, 1);
        node_copy(&replica_node->node, node);
        HASH_ADD(hh, crdt_replica_list->replica_node_hash, node, sizeof(node_t), replica_node);
    }
    return;
}

void build_keyspace_replica_list(const struct swarmkv_reply* reply, void * cb_arg)
{
    struct replica_list *keysapce_replica_list=(struct replica_list *)cb_arg;

    for(size_t i=0; i<reply->n_element; i++)
    {
        struct replica_node *replica_node=NULL;
	    replica_node=ALLOC(struct replica_node, 1);
		node_init_from_reply(&replica_node->node, reply->elements[i]);
        HASH_ADD(hh, keysapce_replica_list->replica_node_hash, node, sizeof(node_t), replica_node);
    }

    g_sanity_ctx->reply_num--;
    if(g_sanity_ctx->reply_num==0)
    {
        cluster_sanity_cond_signal(g_sanity_ctx);
    }
    return;
}

void destroy_relica_list_hash(struct cluster_sanity_ctx *ctx)
{
    struct replica_list *keyspace_list=NULL, *crdt_list=NULL, *tmp_list=NULL;
    struct replica_node *keyspace_node_addr=NULL, *crdt_node_addr=NULL, *tmp_node_addr=NULL;

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

struct swarmkv_reply *cluster_sanity_command(struct swarmkv *db, char *argv[], size_t argc)
{
    int need_fix=0;
    int timed_wait_rv;

    struct swarmkv_reply *keyspace_keys_reply=NULL;
    struct swarmkv_reply *nodes_reply=NULL, *reply=NULL;

    if(argc < 3)
    {
        reply = swarmkv_reply_new_error(error_wrong_number_of_arg, "CLUSTER SANITY");
        return reply;
    }
	char *cmd_cluster_nodes[]={"cluster", "nodes", "brief"};
    nodes_reply=cluster_nodes_command(db, cmd_cluster_nodes, 3);
    if(nodes_reply->type!=SWARMKV_REPLY_ARRAY)
    {
        return nodes_reply;
    }
	node_t active_nodes[SWARMKV_MAX_NODE_NUM];
	size_t n_active_node=nodes_reply->n_element;
	for(size_t i=0; i<nodes_reply->n_element; i++)
	{
		node_init_from_string(active_nodes+i, nodes_reply->elements[i]->str);
	}
    if(0==strcasecmp(argv[2], "heal"))
	{
		need_fix=1;
	}

    struct cluster_sanity_ctx *ctx=g_sanity_ctx=ALLOC(struct cluster_sanity_ctx, 1);
    pthread_cond_init(&ctx->cond, NULL);
	pthread_mutex_init(&ctx->mutex, NULL);

    //Step1 Build the keyspace replica list
    for(size_t i=0; i<n_active_node; i++)
    {
        keyspace_keys_reply=swarmkv_command_on(db, active_nodes[i].addr, "keyspace keys *");
        swarmkv_reply_merge_array(&reply, keyspace_keys_reply);
        if(keyspace_keys_reply->type==SWARMKV_REPLY_NIL)
        {
            swarmkv_reply_free(keyspace_keys_reply);
        }
        keyspace_keys_reply=NULL;
    }

    if(reply)
    {
        ctx->reply_num=reply->n_element;
    }

    for(size_t i=0; reply && i<reply->n_element; i++)
    {
        struct replica_list *keysapce_replica_list=NULL;
        HASH_FIND(hh, ctx->keyspace_replica_hash, reply->elements[i]->str, reply->elements[i]->len, keysapce_replica_list);
        if(!keysapce_replica_list)
        {
            keysapce_replica_list=ALLOC(struct replica_list, 1);
            keysapce_replica_list->key=sdsnewlen(reply->elements[i]->str, reply->elements[i]->len);
            keysapce_replica_list->len=reply->elements[i]->len;
            HASH_ADD_KEYPTR(hh, ctx->keyspace_replica_hash, keysapce_replica_list->key, keysapce_replica_list->len, keysapce_replica_list);
            swarmkv_async_command_on(db, build_keyspace_replica_list, keysapce_replica_list, NULL, "keyspace rlist %s", keysapce_replica_list->key);
        }
    }
    if(reply)
    {
        swarmkv_reply_free(reply);
    }

    //Step2 Build the crdt replica list
    for(size_t i=0; i<n_active_node; i++)
    {
        reply=swarmkv_command_on(db, active_nodes[i].addr, "crdt keys *");
        build_crdt_replica_list(reply, ctx, active_nodes+i);
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
    struct replica_node *keyspace_node_addr=NULL, *crdt_node_addr=NULL, *tmp_node_addr=NULL;

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
                atomic_inc(&ctx->reply_num);
                ctx->heal_replica++;
                swarmkv_async_command_on(db,  exec_cmd_generic_callback, ctx, NULL, "keyspace rdel %s %s", keyspace_list->key, keyspace_node_addr->node.addr);
            }
        }

        if(crdt_list)
        {
            //Step3.1 foreach keysapce list, if keysapce node not in crdt list, keyspace rdel node
            HASH_ITER(hh, keyspace_list->replica_node_hash, keyspace_node_addr, tmp_node_addr)
            {
                HASH_FIND(hh, crdt_list->replica_node_hash, &keyspace_node_addr->node, sizeof(node_t), crdt_node_addr);
                if(!crdt_node_addr && !need_fix)
                {
                    r=swarmkv_reply_new_string(keyspace_list->key, sdslen(keyspace_list->key));
                    swarmkv_reply_append_string(&reply, r);
                }
                if(!crdt_node_addr && need_fix)
                {
                    atomic_inc(&ctx->reply_num);
                    ctx->heal_replica++;
                    swarmkv_async_command_on(db, exec_cmd_generic_callback, ctx, NULL, "keyspace rdel %s %s", keyspace_list->key, keyspace_node_addr->node.addr);
                }
            }
            //Step3.2 foreach crdt list, if crdt node not in keyspace list, keyspace radd node
            HASH_ITER(hh, crdt_list->replica_node_hash, crdt_node_addr, tmp_node_addr)
            {
                HASH_FIND(hh, keyspace_list->replica_node_hash, &crdt_node_addr->node, sizeof(node_t), keyspace_node_addr);
                if(!keyspace_node_addr && !need_fix)
                {
                    r=swarmkv_reply_new_string(keyspace_list->key, sdslen(keyspace_list->key));
                    swarmkv_reply_append_string(&reply, r);
                }
                if(!keyspace_node_addr && need_fix)
                {
                    atomic_inc(&ctx->reply_num);
                    ctx->heal_replica++;
                    swarmkv_async_command_on(db, exec_cmd_generic_callback, ctx, NULL, "keyspace radd %s %s", crdt_list->key, crdt_node_addr->node.addr);
                    exec_cmd_crdt_join(db,ctx, keyspace_list, crdt_node_addr, crdt_list->key);
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
                atomic_inc(&ctx->reply_num);
                ctx->heal_replica++;
                swarmkv_async_command_on(db, exec_cmd_generic_callback, ctx, NULL, "keyspace radd %s %s", crdt_list->key, crdt_node_addr->node.addr);
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
        reply=swarmkv_reply_new_integer(ctx->heal_replica);
        goto finish;
    }

    if(!reply)
    {
        reply=swarmkv_reply_new_integer(0);
    }
finish:
    destroy_relica_list_hash(ctx);
    g_sanity_ctx=NULL;
	return reply;
}

struct swarmkv_reply *attach_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
    if(argc<2)
	{
		reply=swarmkv_reply_new_error("invalid arguments number");
		return reply;
	}
	char IP[128];
	unsigned int port;
	int n_read=sscanf(argv[1], "%[^:]:%u", IP, &port);
	if(n_read!=2)
	{
		reply=swarmkv_reply_new_error("ERR expected address format is `IP:port`.");
		return reply;
	}
	g_runtime.is_attaching=1;
	strncpy(g_runtime.target, argv[1], sizeof(g_runtime.target));
	snprintf(g_runtime.prompt, sizeof(g_runtime.prompt), "%s@%s> ", argv[1], g_config.db_name);

    reply=swarmkv_reply_new_status("OK");
    return reply;
}

struct swarmkv_reply *detach_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	g_runtime.is_attaching=0;
    snprintf(g_runtime.prompt, sizeof(g_runtime.prompt), "%s> ", g_config.db_name);
    reply=swarmkv_reply_new_status("OK");
    return reply;
}

typedef struct swarmkv_reply *cluster_command_func(struct swarmkv *db, char *argv[], size_t argc);

struct cluster_cmd_spec
{
	const char *name;
	const char *hint;
	cluster_command_func *func;
};
struct cluster_cmd_spec cluster_cmds[]={
	{"CLUSTER KEYS", "pattern", cluster_keys_command},
	{"CLUSTER INFO", "[section]", cluster_info_command},
	{"CLUSTER NODES", "[breif | verbose]", cluster_nodes_command},
	{"CLUSTER SLOTS", "", cluster_slots_command},
	{"CLUSTER ADDSLOTOWNER", "IP:port [IP:port ...]", cluster_addslotowner_command},
	{"CLUSTER CREATE", "cluster-name IP:port [IP:port ...]", cluster_create_command},
    {"CLUSTER SANITY", "check | heal", cluster_sanity_command},
    {"ATTACH", "IP:port", attach_command},
    {"DETACH", "", detach_command}
};

int is_cluster_command(int argc, char *argv[])
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0;
	char line[256];
    if(argc==1)
    {
        snprintf(line, sizeof(line), "%s", argv[0]);
    }
    else
    {
        snprintf(line, sizeof(line), "%s %s", argv[0], argv[1]);
    }

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
char *get_cluster_command_hint(const char* cmd_name)
{
    int argc, buflen = strlen(cmd_name);
    sds *argv = sdssplitargs(cmd_name,&argc);
    int endspace = buflen && isspace(cmd_name[buflen-1]);

    if (argc == 0)
    {
        sdsfreesplitres(argv,argc);
        return NULL;
    }

	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0, j=0;
	for(i=0; i<n_cmd; i++)
	{
        int argc_cmd;
        sds *argv_cmd = sdssplitargs(cluster_cmds[i].name, &argc_cmd);
        sds params=sdsdup(argv[0]);
        for(j=1; j<argc && argc==argc_cmd; j++)
        {
            params = sdscatprintf(params, " %s", argv[j]);
        }
        sdsfreesplitres(argv_cmd,argc_cmd);

		if(0==strcasecmp(cluster_cmds[i].name, params))
		{
            sds hint = sdsnew(cluster_cmds[i].hint);
            int toremove = argc - argc_cmd;
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
    if(argc==1)
    {
        snprintf(line, sizeof(line), "%s", argv[0]);
    }
    else
    {
        snprintf(line, sizeof(line), "%s %s", argv[0], argv[1]);
    }

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

    if (argv){
        sdsfreesplitres(argv, argc);
	}
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
	char *p=NULL;
	static char buff[512];
	int offset=0;
	/*
	for(size_t i=0; i<strlen(cmd); i++)
	{
		if((cmd[i] >= '0' && cmd[i] <= '9')||cmd[i]==' ')
		{
            offset++;
		}
		else
		{
			break;
		}
	}
	*/
	p=swarmkv_get_command_hint(g_config.db, cmd+offset);
	if(!p)
		p=get_cluster_command_hint(cmd+offset);

	if(p)
	{
		*color = 90;
		*bold = 0;
		snprintf(buff, sizeof(buff), " %s", p);
        sdsfree(p);
		return buff;
	}

	return NULL;
}

int main(int argc, char * argv[])
{
	const char *history_file=".swarmkv-cli-history.txt";
	char *line;
	struct swarmkv_reply *reply=NULL;
	memset(&g_config, 0, sizeof(g_config));
	g_config.consul_port=8500;
	strcpy(g_config.consul_host, "127.0.0.1");
    int i;
	sds *exec_argv=NULL, *attach_argv=NULL;
	int exec_argc=0, attach_argc=1;
    for (i = 1; i < argc; i++)
	{
        int lastarg = i==argc-1;
		if (!strcmp(argv[i], "-n") && !lastarg)
		{
			strncpy(g_config.db_name, argv[++i], sizeof(g_config.db_name));
		}
		else if(!strcmp(argv[i], "-c") && !lastarg)
		{
            int n_read=sscanf(argv[++i], "%[^:]:%u", g_config.consul_host, &g_config.consul_port);
	        if(n_read!=2)
        	{
		        reply=swarmkv_reply_new_error("ERR expected address format is `IP:port`.");
                swarmkv_reply_print(reply, stdout);
		        swarmkv_reply_free(reply);
                usage();
		        return 0;
        	}
		}
		else if(!strcmp(argv[i], "-l") && !lastarg)
		{
			sscanf(argv[++i], "%u", &g_config.cluster_port);
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
        else if(!strcmp(argv[i], "--attach") && !lastarg)
		{
			attach_argv=ALLOC(sds, argc);
            attach_argv[0]=sdsnew("ATTACH");
            int j = i;
			while (++i < argc && argv[i][0] != '-')
			{
                j++;
				attach_argv[attach_argc]=sdsnew(argv[i]);
				attach_argc++;
			}
            if (j > i) j--;
			reply=attach_command(NULL, attach_argv, attach_argc);
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
		swarmkv_reply_print(reply, stdout);
		swarmkv_reply_free(reply);
		return 1;
	}
	
	if(!strlen(g_config.db_name))
	{
		usage();
		return 0;
	}
	snprintf(g_runtime.prompt, sizeof(g_runtime.prompt), "%s> ", g_config.db_name);
	struct log_handle * logger=log_handle_create("swarmkv-cli.log", 0);
	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_dryrun(opts);
	swarmkv_options_set_cluster_port(opts, g_config.cluster_port);//listen on random port
    swarmkv_options_set_consul_host(opts, g_config.consul_host);
	swarmkv_options_set_consul_port(opts, g_config.consul_port);
	swarmkv_options_set_worker_thread_number(opts, 1);
	swarmkv_options_set_caller_thread_number(opts, 1);
	swarmkv_options_set_logger(opts, logger);
	char *err=NULL;
	g_config.db=swarmkv_open(opts, g_config.db_name, &err);
	if(err)
	{
		printf("Open DB `%s` error: %s\n", g_config.db_name, err);
		free(err);
		free(exec_argv);
		err=NULL;
		return 0;
	}
	swarmkv_register_thread(g_config.db);
	//Run swarmkv-cli --exec commands.
	if(exec_argc)
	{
		if(is_cluster_command(exec_argc, exec_argv))
		{
			reply=cluster_command_argv(g_config.db, exec_argc, exec_argv);
		}
		else
		{
			reply=swarmkv_command_on_argv(g_config.db, get_attach_target(), exec_argc, exec_argv);
		}
		swarmkv_reply_print(reply, stdout);
		swarmkv_reply_free(reply);
		for(i=0; i<exec_argc; i++) sdsfree(exec_argv[i]);
		free(exec_argv);
		goto clean;
	}
	
	/* Set the completion callback. This will be called every time the
	* user uses the <tab> key. */
	linenoiseSetCompletionCallback(completion);
	linenoiseSetHintsCallback(hints);

	linenoiseHistoryLoad(history_file); /* Load the history at startup */
	long long run_times=0;
	double run_interval_sec=0;
	int offset=0;
	while((line = linenoise(g_runtime.prompt)) != NULL)
	{
		if(0==strlen(line))
		{
			continue;
		}
		if(0==strcasecmp(line, "exit")||0==strcasecmp(line, "quit")||0==strcasecmp(line, "q"))
		{
			break;
		}
		exec_argv=sdssplitargs(line, &exec_argc);
		if(!exec_argc)
		{
			reply=swarmkv_reply_new_error("invalid command format");
			swarmkv_reply_print(reply, stdout);
			swarmkv_reply_free(reply);
			continue;
		}
		run_times=1;
		run_interval_sec=0;
		offset=0;
		if(is_number(exec_argv[0], sdslen(exec_argv[0]), &run_times))
		{
			offset++;
			if(exec_argc>1 && is_double(exec_argv[1], &run_interval_sec))
			{
				offset++;
			}
			else
			{
				run_interval_sec=0;
			}
		}
		else
		{
			run_times=1;
			run_interval_sec=0;
		}
		for(int i=0; i<run_times; i++)
		{
			if(is_cluster_command(exec_argc-offset, exec_argv+offset))
			{
				reply=cluster_command_argv(g_config.db, exec_argc-offset, exec_argv+offset);
			}
			else
			{
				reply=swarmkv_command_on_argv(g_config.db, get_attach_target(), exec_argc-offset, exec_argv+offset);
			}
			swarmkv_reply_print(reply, stdout);
			if(run_interval_sec && reply->type != SWARMKV_REPLY_ERROR)
			{
				long long utime = run_interval_sec*1000000;
				struct timespec tv;
				tv.tv_sec = utime / 1000000;
				tv.tv_nsec = (utime % 1000000) * 1000;
				nanosleep(&tv, NULL);
			}
			swarmkv_reply_free(reply);
			reply=NULL;
		}
		linenoiseHistoryAdd(line); // Add to the history. 
		linenoiseHistorySave(history_file); // Save the history on disk. 
		
		free(line);
		
	    if (exec_argv)
	        sdsfreesplitres(exec_argv, exec_argc);
	}
clean:
	swarmkv_close(g_config.db);
	log_handle_destroy(logger);
	return 0;
}



