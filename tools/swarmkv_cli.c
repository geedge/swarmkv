#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
#define SWARMKV_CLI_VERSION	"2.0.0 Islamabad F7"

static void usage(void)
{
	fprintf(stderr,
	"swarmkv-cli %s\n"
	"\n"
	"Usage: swarmkv-cli [OPTIONS] [cmd [arg [arg ...]]]\n"
	"  -n <cluster-name>  Cluster name used for orchestration.\n"
	"  -p <consul-port>   Consul Agent port (default: 8500).\n"
	"  --cluster-create	<cluster-name> IP port [IP port]  Create orchestration information in Consul KV.\n",
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
	size_t slots_per_node=SWARMKV_SLOT_NUM/(argc/2);
	struct key_slot slots[SWARMKV_SLOT_NUM];
	size_t i=0, nth_node=0;
	int ret=0;
	memset(slots, 0, sizeof(slots));
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		nth_node=i/slots_per_node;
		ip=argv[nth_node*2];
		port=argv[nth_node*2+1];
		slots[i].slot_id=i;
		strncpy(slots[i].owner.addr.ip_addr, ip, sizeof(slots[i].owner.addr));
		slots[i].owner.addr.p2p_port=atoi(port);
	}
	sds slot_json=NULL;
	slot_json=keyslots2json(slots, sizeof(struct key_slot), 0, SWARMKV_SLOT_NUM);
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
	slots=ALLOC(struct key_slot, SWARMKV_SLOT_NUM);
	json2keyslots(slots_reply->str, slots, sizeof(struct key_slot), 0, SWARMKV_SLOT_NUM);
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
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

	
	qsort(slots, SWARMKV_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_owner);
	struct key_slot *tmp=slots;
	size_t n_active_node=1;
	for(i=0; i<SWARMKV_SLOT_NUM; i++)
	{
		if(0!=swarmkv_node_compare(&(tmp->owner), &(slots[i].owner)))
		{
			n_active_node++;
			tmp=slots+i;
		}
	}
	int expected_rebalanced_slot_num=SWARMKV_SLOT_NUM/(n_active_node+1);
	int expected_rebalanced_slot_num_per_node=expected_rebalanced_slot_num/n_active_node;
	int rebalanced_slot_idx[SWARMKV_SLOT_NUM]={0};
	tmp=slots;
	for(i=0, j=0, actual_rebalanced_slot_num=0; i<SWARMKV_SLOT_NUM && actual_rebalanced_slot_num<expected_rebalanced_slot_num; i++)
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
	qsort(slots, SWARMKV_SLOT_NUM, sizeof(struct key_slot), slot_cmp_by_id);
	sds slot_json=NULL;
	slot_json=keyslots2json(slots, sizeof(struct key_slot), 0, SWARMKV_SLOT_NUM);

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
	{"CLUSTER CREATE", "cluster-name IP port [IP port]", cluster_create_command}
};
int is_cluster_command(const char *line)
{
	size_t n_cmd=sizeof(cluster_cmds)/sizeof(struct cluster_cmd_spec);
	size_t i=0;
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

struct swarmkv_reply *cluster_command(struct swarmkv *db, const char *line)
{
	int argc=0;
	sds *argv=NULL;
	argv=sdssplitargs(line, &argc);

	cluster_command_func *cmd_func=get_cluster_func(line);
	struct swarmkv_reply *reply=NULL;
	reply=cmd_func(db, argv, argc);

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
		case SWARMKV_REPLY_STATUS:
			printf("%s\n", reply->str);
			break;
		case SWARMKV_REPLY_STRING:
			printf("\"%s\"\n", reply->str);
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
		err=NULL;
		return 0;
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
		if(is_cluster_command(line))
		{
			reply=cluster_command(g_config.db, line);
		}
		else
		{
			reply=swarmkv_command(g_config.db, line);		
		}
		print_reply(reply);
		swarmkv_reply_free(reply);
		reply=NULL;
		linenoiseHistoryAdd(line); // Add to the history. 
		linenoiseHistorySave(history_file); // Save the history on disk. 
		
		free(line);
	}
	return 0;

}



