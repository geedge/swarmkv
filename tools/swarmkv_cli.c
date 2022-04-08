#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sds.h"
#include "swarmkv_common.h"
#include "linenoise/linenoise.h"
#include "swarmkv/swarmkv.h"

struct swarmkv *global_db=NULL;

struct swarmkv_reply *cluster_keys_command(struct swarmkv* db, char *argv[], size_t argc)
{
	struct swarmkv_reply *nodes_reply=NULL, *keyspace_keys_reply=NULL, *reply=NULL;
	char uuid[38], ip[64], port[16], membership[16];
	size_t i=0;
	nodes_reply=swarmkv_command(db, "keyspace nodes");
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
struct swarmkv_reply *cluster_addnode_command(struct swarmkv* db, char *argv[], size_t argc)
{
	return NULL;
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
	{"CLUSTER ADDNODE", "IP port", cluster_addnode_command}
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
	size_t i=0;
	for(i=0; i<n_cmd; i++)
	{
		if(0==strncasecmp(line, cluster_cmds[i].name, strlen(cluster_cmds[i].name)))
		{
			return cluster_cmds[i].func;
		}
	}
	return NULL;
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
	n_matched=swarmkv_get_possible_command_name(global_db, buff, possible_cmd, 32);
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
	p=swarmkv_get_command_hint(global_db, cmd);
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
	char db_name[128]="";
	const char *history_file="swarmkv-cli-history.txt";
	char *line;
	char *prgname = argv[0];
	unsigned int consul_port=0;
	int oc;
	while((oc=getopt(argc, argv, "n:p:"))!=-1)
	{
		switch(oc)
		{
			case 'n':
				strncpy(db_name, optarg, sizeof(db_name));
				break;
			case 'p':
				sscanf(optarg, "%u", &consul_port);
				break;
			default:	
				fprintf(stderr, "Usage: %s [-n global_db-name] [-p consul-port]\n", prgname);
				break;
		}
	}
	if(!strlen(db_name))
	{
		fprintf(stderr, "need [-n global_db-name]\n");
		return 0;
	}
	struct swarmkv_options *opts=swarmkv_options_new();
	swarmkv_options_set_dryrun(opts);
	swarmkv_options_set_worker_thread_number(opts, 2);
	char *err=NULL;
	global_db=swarmkv_open(opts, db_name, &err);
	if(err)
	{
		fprintf(stderr, "Open DB %s error: %s", db_name, err);
		free(err);
		err=NULL;
		return 0;
	}
	/* Set the completion callback. This will be called every time the
	* user uses the <tab> key. */
	linenoiseSetCompletionCallback(completion);
	linenoiseSetHintsCallback(hints);

	linenoiseHistoryLoad(history_file); /* Load the history at startup */

	struct swarmkv_reply *reply=NULL;
	char prompt[256];
	snprintf(prompt, sizeof(prompt), "%s> ", db_name);
	while((line = linenoise(prompt)) != NULL)
	{
		if(0==strlen(line))
		{
			continue;
		}
		if(is_cluster_command(line))
		{
			reply=cluster_command(global_db, line);
		}
		else
		{
			reply=swarmkv_command(global_db, line);		
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



