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
#include "swarmkv_tools.h"

#define	CLUSTER_MANAGER_MODE() (g_config.cluster_mgr_cmd.cluster_name != NULL)
#define SWARMKV_CLI_VERSION	"3.0.0 Islamabad F7"

struct runtime
{
	char prompt[512];
	int is_attaching;
	char target[128];
};
struct runtime g_runtime;

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

struct swarmkv_reply *monitor_command(struct swarmkv *db, char *argv[], size_t argc)
{
	struct swarmkv_reply *reply=NULL;
	const char *target=get_attach_target();
	if(!target)
	{
		reply = swarmkv_reply_new_error("ERR `MONITOR` command requires attaching to a node.");
	}
	else
	{
		if(argc>1)
		{
			reply=swarmkv_command_on(db, target, "monreg %s %s", swarmkv_self_address(db), argv[1]);
		}
		else
		{
			reply=swarmkv_command_on(db, target, "monreg %s", swarmkv_self_address(db));
		}
	}
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
    {"DETACH", "", detach_command},
	{"MONITOR", "[pattern]", monitor_command}
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
	//If g_config.cluster_port is not specified (cluster_port==0), listen on random port.
	swarmkv_options_set_cluster_port(opts, g_config.cluster_port);
    swarmkv_options_set_consul_host(opts, g_config.consul_host);
	swarmkv_options_set_consul_port(opts, g_config.consul_port);
	swarmkv_options_set_worker_thread_number(opts, 1);
	swarmkv_options_set_caller_thread_number(opts, 1);
	swarmkv_options_set_cluster_timeout_us(opts, 1000*1000);
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
			size_t exec_argv_len[exec_argc];
			for(int i=0; i<exec_argc; i++)
			{
				exec_argv_len[i]=sdslen(exec_argv[i]);
			}
			reply=swarmkv_command_on_argv(g_config.db, get_attach_target(), exec_argc, (const char**)exec_argv, exec_argv_len);
		}
		for(i=0; i<exec_argc; i++) sdsfree(exec_argv[i]);
		free(exec_argv);
		swarmkv_reply_print(reply, stdout);
		swarmkv_reply_free(reply);
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
				size_t exec_argv_len[exec_argc-offset];
				for(int i=0; i<exec_argc-offset; i++)
				{
					exec_argv_len[i]=sdslen(exec_argv[i+offset]);
				}
				reply=swarmkv_command_on_argv(g_config.db, get_attach_target(), exec_argc-offset, (const char**)exec_argv+offset, exec_argv_len);
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



