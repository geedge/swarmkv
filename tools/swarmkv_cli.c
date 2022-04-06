#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "linenoise/linenoise.h"
#include "swarmkv/swarmkv.h"

struct swarmkv *global_db=NULL;

void completion(const char *buf, linenoiseCompletions *lc)
{
	size_t n_matched=0, i=0;
	const char *possible_cmd[32];
	n_matched=swarmkv_get_possible_command_name(global_db, buf, possible_cmd, 32);
	for(i=0; i<n_matched; i++)
	{
		linenoiseAddCompletion(lc, possible_cmd[i]);
	}
}

char *hints(const char *buf, int *color, int *bold)
{
	const char *p=NULL;
	static char buff[512];
	p=swarmkv_get_command_hint(global_db, buf);
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

	/* Load history from file. The history file is just a plain text file
	* where entries are separated by newlines. */
	linenoiseHistoryLoad("history.txt"); /* Load the history at startup */

	/* Now this is the main loop of the typical linenoise-based application.
	* The call to linenoise() will block as long as the user types something
	* and presses enter.
	*
	* The typed string is returned as a malloc() allocated string by
	* linenoise, so the user needs to free() it. */


	struct swarmkv_reply *reply=NULL;
	char prompt[256];
	snprintf(prompt, sizeof(prompt), "%s> ", db_name);
	while((line = linenoise(prompt)) != NULL)
	{
		if(0==strlen(line))
		{
			continue;
		}
		reply=swarmkv_command(global_db, line);		
		print_reply(reply);
		swarmkv_reply_free(reply);
		reply=NULL;
		linenoiseHistoryAdd(line); // Add to the history. 
		linenoiseHistorySave("history.txt"); // Save the history on disk. 
		
		/*
		if (line[0] != '\0' && line[0] != '/')
		{
			printf("echo: '%s'\n", line);
			linenoiseHistoryAdd(line); // Add to the history. 
			linenoiseHistorySave("history.txt"); // Save the history on disk. 
		}
		else if (!strncmp(line,"/historylen", 11))
		{
			// The "/historylen" command will change the history len. 
			int len = atoi(line+11);
			linenoiseHistorySetMaxLen(len);
		}
		else if(!strncmp(line, "/mask", 5))
		{
			linenoiseMaskModeEnable();
		}
		else if(!strncmp(line, "/unmask", 7))
		{
			linenoiseMaskModeDisable();
		}
		else if (line[0] == '/')
		{
			printf("Unreconized command: %s\n", line);
		}
		*/
		free(line);
	}
	return 0;

}
