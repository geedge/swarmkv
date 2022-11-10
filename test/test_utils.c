#include "test_utils.h"
#include "swarmkv/swarmkv.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <asm/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>

int swarmkv_cli_create_cluster(const char *cluster_name, const char *node_string)
{
	char cmd_string[1024];
	snprintf(cmd_string, sizeof(cmd_string), "../tools/swarmkv-cli --cluster-create %s %s", cluster_name, node_string);
	return system(cmd_string);
}
int swarmkv_cli_add_node(const char *cluster_name, const char *ip, unsigned int port)
{
	char cmd_string[1024];
	snprintf(cmd_string, sizeof(cmd_string), "../tools/swarmkv-cli -n %s --exec cluster addnode %s:%u", cluster_name, ip, port);
	return system(cmd_string);

}
void wait_for_sync()
{
	usleep(1000*1000);
}

struct cmd_exec_arg* cmd_exec_arg_new(void)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)calloc(sizeof(struct cmd_exec_arg), 1);
	pthread_cond_init(&arg->cond, NULL);
	pthread_mutex_init(&arg->mutex, NULL);
	arg->callback_invoking_tid=pthread_self();
	return arg;
}
void cmd_exec_arg_set_cb(struct cmd_exec_arg *arg, proc_result_callback_t *cb, void* cb_arg)
{
	arg->cb=cb;
	arg->cb_uarg=cb_arg;
}
void cmd_exec_arg_free(struct cmd_exec_arg* arg)
{
	pthread_cond_destroy(&arg->cond);
	pthread_mutex_destroy(&arg->mutex);
	
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}

	free(arg);
}
void cmd_exec_arg_clear(struct cmd_exec_arg* arg)
{
	arg->success=0;
	arg->is_callback_executed=0;
	arg->callback_invoking_tid=pthread_self();
	arg->callback_invoking_tid=0;
	arg->is_sync_callback=0;
	return;
}

void cmd_exec_arg_expect_cstring(struct cmd_exec_arg* arg, const char* string)
{
	arg->expected_reply.type=SWARMKV_REPLY_STRING;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup(string);
	arg->expected_reply.len=strlen(string);

}
void cmd_exec_arg_expect_integer(struct cmd_exec_arg* arg, long long integer)
{
	arg->expected_reply.type=SWARMKV_REPLY_INTEGER;
	arg->expected_reply.integer=integer;
}
void cmd_exec_arg_expect_array(struct cmd_exec_arg* arg, size_t n_element)
{
	arg->expected_reply.type=SWARMKV_REPLY_ARRAY;
	arg->expected_reply.n_element=n_element;
}

void cmd_exec_arg_expect_OK(struct cmd_exec_arg* arg)
{
	arg->expected_reply.type=SWARMKV_REPLY_STATUS;
	if(arg->expected_reply.str)
	{
		free(arg->expected_reply.str);
		arg->expected_reply.str=NULL;
	}
	arg->expected_reply.str=strdup("OK");
	arg->expected_reply.len=2;
}
void cmd_exec_arg_expect_NIL(struct cmd_exec_arg* arg)
{
	arg->expected_reply.type=SWARMKV_REPLY_NIL;
}

void error_pthread_cond_timedwait(const int timed_wait_rv)
{
    fprintf(stderr, "Conditional timed wait, failed.\n");
    switch (timed_wait_rv)
    {
        case ETIMEDOUT:
            fprintf(stderr, "The time specified by abstime to pthread_cond_timedwait() has passed.\n");
            break;
        case EINVAL:
            fprintf(stderr, "The value specified by abstime, cond or mutex is invalid.\n");
            break;
        case EPERM:
            fprintf(stderr, "The mutex was not owned by the current thread at the time of the call.\n");
            break;
        default:
            break;
    }
}

int cmd_exec_arg_wait(struct cmd_exec_arg* arg, long timeout_ms)
{
	struct timespec max_wait = {0, 0};
	clock_gettime(CLOCK_REALTIME, &max_wait);
	max_wait.tv_sec+=timeout_ms/1000;
	max_wait.tv_nsec+=(timeout_ms%1000)*1000*1000;	
	int timed_wait_rv=0;
	if(arg->is_callback_executed)
	{
		return arg->success;
	}
	pthread_mutex_lock(&arg->mutex);
	timed_wait_rv=pthread_cond_timedwait(&arg->cond, &arg->mutex, &max_wait);
	pthread_mutex_unlock(&arg->mutex);
	if(timed_wait_rv&& !(arg->is_callback_executed))
	{
		error_pthread_cond_timedwait(timed_wait_rv);
		return -1;
	}
	else
	{
		return arg->success;
	}
}

void cmd_exec_arg_success(struct cmd_exec_arg* arg)
{
	arg->is_callback_executed=1;
	arg->success=1;
	arg->callback_invoking_tid = pthread_self();
	if(arg->callback_invoking_tid==arg->api_invoking_tid)
	{
		arg->is_sync_callback=1;
	}
	if(arg->cb)
	{
		arg->cb(arg, arg->cb_uarg);
	}
	pthread_cond_signal(&arg->cond);
}
void cmd_exec_arg_failed(struct cmd_exec_arg* arg)
{
	arg->is_callback_executed=1;
	arg->success=0;
	arg->callback_invoking_tid=pthread_self();
	if(arg->callback_invoking_tid==arg->api_invoking_tid)
	{
		arg->is_sync_callback=1;
	}
	if(arg->cb)
	{
		arg->cb(arg, arg->cb_uarg);
	}
	pthread_cond_signal(&arg->cond);
}
void cmd_exec_generic_callback(const struct swarmkv_reply* reply, void * cb_arg)
{
	struct cmd_exec_arg* arg=(struct cmd_exec_arg*)cb_arg;
	if(0==reply_compare(reply, &(arg->expected_reply)))
	{
		cmd_exec_arg_success(arg);
	}
	else
	{
		if(arg->print_reply_on_fail)
		{
			swarmkv_reply_print(reply, stdout);
		}
		cmd_exec_arg_failed(arg);
	}
	return;
}

int reply_compare(const struct swarmkv_reply *r1, const struct swarmkv_reply *r2)
{
	size_t i=0;
	int ret=0;
	if(r1->type!=r2->type) return r1->type-r2->type;
	switch(r1->type)
	{
		case SWARMKV_REPLY_INTEGER:
			return r1->integer - r2->integer;
			break;
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_NODE:
			if(r1->len!=r2->len)
			{
				return r1->len - r2->len;
			}
			else
			{
				return memcmp(r1->str, r2->str, r1->len);
			}
			break;
		case SWARMKV_REPLY_NIL:
			return 0;
			break;
		case SWARMKV_REPLY_ARRAY:
			return (r1->n_element - r2->n_element);
			
			if(r1->n_element != r2->n_element)
			{
				return r1->n_element - r2->n_element;
			}
			for(i=0; i<r1->n_element; i++)
			{
				ret=reply_compare(r1->elements[i], r2->elements[i]);
				if(ret!=0)
				{
					return ret;
				}
			}
			break;
		default:
			ret=0;
	}
	return 0;
}


