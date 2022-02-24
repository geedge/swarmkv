#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_limits.h"
#include <stdlib.h>

void swarmkv_reply_free(struct swarmkv_reply *reply)
{
	size_t i=0;
	switch (reply->type)
	{
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
			free(reply->str);
			reply->str=NULL;
			break;
		case SWARMKV_REPLY_ARRAY:
			for(i=0; i<reply->n_element; i++)
			{
				swarmkv_reply_free(reply->element[i]);
				reply->element[i]=NULL;
			}
			break;
		default:
			break;
	}
	free(reply);
}
struct swarmkv_cmd* swarmkv_cmd_new(enum swarmkv_operator op, size_t n_args)
{
	struct swarmkv_cmd* cmd=ALLOC(struct swarmkv_cmd, 1);
	cmd->op=op;
	cmd->n_args=n_args;
	assert(cmd->n_args<MAX_CMD_ARGS);
	return cmd;
}
void swarmkv_cmd_free(struct swarmkv_cmd* p)
{
	size_t i=0;
	for(i=0; i<p->n_args; i++)
	{
		sdsfree(p->args[i]);
		p->args[i]=NULL;
	}
	free(p);
}
struct swarmkv_cmd* swarmkv_cmd_dup(const struct swarmkv_cmd* origin)
{
	struct swarmkv_cmd* copy=ALLOC(struct swarmkv_cmd, 1);
	copy->op=origin->op;
	copy->n_args=origin->n_args;
	size_t i=0;
	for(i=0; i<origin->n_args; i++)
	{
		copy->args[i]=sdsdup(origin->args[i]);
	}
	return copy;
}


