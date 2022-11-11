#pragma once
#include "swarmkv/swarmkv.h"
#include <pthread.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif



#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))

int swarmkv_cli_create_cluster(const char* cluster_name, const char *node_string);
int swarmkv_cli_add_node(const char *cluster_name , const char *ip, unsigned int port);

struct cmd_exec_arg;
typedef void proc_result_callback_t(struct cmd_exec_arg* exec_arg, void *uarg);
struct cmd_exec_arg
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int is_callback_executed;
	int success;
	int api_invoking_tid;
	pthread_t callback_invoking_tid;
	int is_sync_callback;
	struct swarmkv_reply expected_reply;
	proc_result_callback_t *cb;
	void *cb_uarg;
	int print_reply_on_fail;
	int diable_sync_check;
};
struct cmd_exec_arg* cmd_exec_arg_new(void);
void cmd_exec_arg_set_cb(struct cmd_exec_arg *arg, proc_result_callback_t *cb, void* cb_arg);
void cmd_exec_arg_free(struct cmd_exec_arg* arg);
void cmd_exec_arg_clear(struct cmd_exec_arg* arg);
void cmd_exec_arg_expect_cstring(struct cmd_exec_arg* arg, const char* string);
void cmd_exec_arg_expect_integer(struct cmd_exec_arg* arg, long long integer);
void cmd_exec_arg_expect_array(struct cmd_exec_arg* arg, size_t n_element);
void cmd_exec_arg_expect_OK(struct cmd_exec_arg* arg);
void cmd_exec_arg_expect_NIL(struct cmd_exec_arg* arg);
int cmd_exec_arg_wait(struct cmd_exec_arg* arg, long timeout_ms);
void cmd_exec_arg_success(struct cmd_exec_arg* arg);
void cmd_exec_arg_failed(struct cmd_exec_arg* arg);
void cmd_exec_arg_diable_sync_check(struct cmd_exec_arg* arg);
void cmd_exec_generic_callback(const struct swarmkv_reply* reply, void * cb_arg);

int reply_compare(const struct swarmkv_reply *r1, const struct swarmkv_reply *r2);
void wait_for_sync();

#ifdef __cplusplus
} /* end extern "C" */
#endif

