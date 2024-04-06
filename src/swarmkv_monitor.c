#include "uthash.h"
#include "sds.h"
#include "log.h"
#include "uthash.h"
#include "utlist.h"
#include "swarmkv_monitor.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_error.h"
#include <hdr/hdr_histogram.h>
#include <hdr/hdr_interval_recorder.h>

#include <pthread.h>
#define KEY_LEN_MAX  64
#define SIGNIFICANT_FIGURES 2
#define LOWEST_TRACKABLE_VALUE  1
struct recorder
{
    char key[KEY_LEN_MAX];
    struct hdr_interval_recorder hdr_interval;
    struct hdr_histogram *hdr_previous;
    struct hdr_histogram *hdr_all_time;
    struct timespec last_sampled;
    UT_hash_handle hh;
};
struct recorder *recorder_new(const char *key, long long max_latency_usec)
{
    struct recorder *recorder=ALLOC(struct recorder, 1);
    strncpy(recorder->key, key, sizeof(recorder->key));
    int __attribute__((__unused__))ret=0;
    ret=hdr_interval_recorder_init_all(&recorder->hdr_interval, LOWEST_TRACKABLE_VALUE, max_latency_usec, SIGNIFICANT_FIGURES);
    assert(ret==0);
    hdr_init(1, max_latency_usec, 2, &recorder->hdr_all_time);
    clock_gettime(CLOCK_MONOTONIC, &(recorder->last_sampled));
    return recorder;
}
void recorder_free(struct recorder *recorder)
{
    hdr_interval_recorder_destroy(&recorder->hdr_interval);
    hdr_close(recorder->hdr_all_time);
    recorder->hdr_all_time=NULL;
    hdr_close(recorder->hdr_previous);
    recorder->hdr_previous=NULL;
    free(recorder);
    return;
}
struct recorder_metric
{
    char key[KEY_LEN_MAX];
    long long total_count;
    long long max;
    long long min;
    double stddev, mean;
    long long p50, p80, p90, p95, p99;
};
size_t metric2string(const struct recorder_metric *metric, char *buff, size_t buff_sz)
{
    return snprintf(buff, buff_sz, "p50_usec=%lld "
                                    "p80_usec=%lld "
                                    "p90_usec=%lld "
                                    "p95_usec=%lld "
                                    "p99_usec=%lld "
                                    "max_usec=%lld "
                                    "min_usec=%lld "
                                    "mean_usec=%.2f "
                                    "stddev=%.2f "
                                    "count=%lld"
                                    ,
                                    metric->p50,
                                    metric->p80,
                                    metric->p90,
                                    metric->p95,
                                    metric->p99,
                                    metric->max,
                                    metric->min,
                                    metric->mean,
                                    metric->stddev,
                                    metric->total_count
                                    );
}
void hdr_to_metric(const struct hdr_histogram *hdr, const char *key, struct recorder_metric *metric)
{
    memset(metric, 0, sizeof(struct recorder_metric));
    strncpy(metric->key, key, sizeof(metric->key));
    metric->total_count=hdr->total_count;
    if(metric->total_count==0) return;
    metric->p50=hdr_value_at_percentile(hdr, 50);
    metric->p80=hdr_value_at_percentile(hdr, 80);
    metric->p90=hdr_value_at_percentile(hdr, 90);
    metric->p95=hdr_value_at_percentile(hdr, 95);
    metric->p99=hdr_value_at_percentile(hdr, 99);
    metric->total_count=hdr->total_count;
    metric->max=hdr_max(hdr);
    metric->mean=hdr_mean(hdr);
    metric->min=hdr_min(hdr);
    metric->stddev=hdr_stddev(hdr);
    return;
}
void recorder_commit(struct recorder *recorder)
{
    recorder->hdr_previous=hdr_interval_recorder_sample_and_recycle(&recorder->hdr_interval, recorder->hdr_previous);
    long long __attribute__((__unused__))dropped=hdr_add(recorder->hdr_all_time, recorder->hdr_previous);
    assert(dropped==0);
    return;
}
void recorder_sample(struct recorder *recorder, struct recorder_metric *metric)
{
    recorder_commit(recorder);
    hdr_to_metric(recorder->hdr_all_time, recorder->key, metric);
    return;
}

void recorder_reset(struct recorder *recorder)
{
    recorder->hdr_previous=hdr_interval_recorder_sample_and_recycle(&recorder->hdr_interval, recorder->hdr_previous);
    hdr_reset(recorder->hdr_previous);
    hdr_reset(recorder->hdr_all_time);
    return;
}
void recorder_record(struct recorder *recorder, long long latency_usec, long long corrected)
{
    hdr_interval_recorder_record_corrected_value(&recorder->hdr_interval, latency_usec, corrected);
}
void recorder_table_record_latency(struct recorder **table, const char *key, size_t key_len, long long latency_usec, long long corrected, int add_if_not_exist)
{
    struct recorder *recorder=NULL;
    HASH_FIND(hh, *table, key, key_len, recorder);
    if(!recorder)
    {
        if(add_if_not_exist)
        {
            recorder=recorder_new(key, corrected);
 	        HASH_ADD_KEYPTR(hh, *table, recorder->key, strlen(recorder->key), recorder);
        }
        else
        {
            assert(0);
            return;
        }
    }
    recorder_record(recorder, latency_usec, corrected);
    return;
}
void recorder_table_reset(struct recorder **table)
{
    struct recorder *recorder=NULL, *tmp=NULL;
    HASH_ITER(hh, *table, recorder, tmp)
    {
       recorder_reset(recorder);
    }
    return;
}
void recorder_table_merge(struct recorder **src_tables, size_t n_table,  struct recorder **dst_table)
{
    struct recorder *dst_recorder=NULL;
    struct recorder *src_recorder=NULL, *tmp=NULL;
    for(size_t i=0; i<n_table; i++)
    {
        src_recorder=NULL;
        tmp=NULL;
        HASH_ITER(hh, src_tables[i], src_recorder, tmp)
        {
           
            HASH_FIND(hh, *dst_table, src_recorder->key, strlen(src_recorder->key), dst_recorder);
            if(!dst_recorder)
            {
                dst_recorder=recorder_new(src_recorder->key, src_recorder->hdr_all_time->highest_trackable_value);
                HASH_ADD_KEYPTR(hh, *dst_table, dst_recorder->key, strlen(dst_recorder->key), dst_recorder);
            }
            recorder_commit(src_recorder);
            hdr_add(dst_recorder->hdr_all_time, src_recorder->hdr_all_time);
        }
    }
    return;
}
size_t recorder_table_count(struct recorder **table)
{
    return HASH_COUNT(*table);
}
int recorder_table_sample_key(struct recorder **table, const char *key, struct recorder_metric *metric)
{
    struct recorder *recorder=NULL, *tmp=NULL;
	HASH_ITER(hh, *table, recorder, tmp)
	{
		if(0==strncasecmp(recorder->key, key, strlen(key)))
		{
			 recorder_sample(recorder, metric);
             return 1;
		}
	}
    return 0;
}
void recorder_table_sample_all(struct recorder **table, struct recorder_metric **metrics, size_t *n_metric)
{
    struct recorder *recorder=NULL, *tmp=NULL;
    *n_metric=HASH_COUNT(*table);
    *metrics=ALLOC(struct recorder_metric, *n_metric);
    size_t i=0;
    HASH_ITER(hh, *table, recorder, tmp)
    {
        recorder_sample(recorder, *metrics+i);
        i++;
    }
    return;
}
void recorder_table_free(struct recorder **table)
{
    struct recorder *recorder=NULL, *tmp=NULL;
	HASH_ITER(hh, *table, recorder, tmp)
	{
        HASH_DEL(*table, recorder);
        recorder_free(recorder);
	}
}
struct swarmkv_reply *recorder_metric_to_reply(const struct recorder_metric *metric)
{
    char metric_buff[1024];
    metric2string(metric, metric_buff, sizeof(metric_buff));
    struct swarmkv_reply *reply=swarmkv_reply_new_array(2);
    reply->elements[0]=swarmkv_reply_new_string_fmt(metric->key);
    reply->elements[1]=swarmkv_reply_new_string_fmt(metric_buff);
    return reply;
}
struct monitor_client
{
    node_t node;
    sds pattern;
    struct monitor_client *next;
};
struct monitor_client *monitor_client_new(const node_t *node, sds pattern)
{
    struct monitor_client *client=ALLOC(struct monitor_client, 1);
    node_copy(&client->node, node);
    client->pattern = pattern? sdsdup(pattern):NULL;
    return client;
}
void monitor_client_free(struct monitor_client *client)
{
    sdsfree(client->pattern);
    free(client);
    return;
}
struct swarmkv_monitor_thread
{
    int n_client;
    struct monitor_client *clients;
    pthread_mutex_t mutex;
};
struct swarmkv_monitor
{
    struct swarmkv_module module;
    struct recorder *commands;
    struct recorder *events;
    struct recorder **peers;
    long long max_latency_usec;
    int significant_figures;
    int nr_worker_threads;
    struct swarmkv_monitor_thread *threads;
    pthread_mutex_t latency_execute_mutex; //only one latency command can be executed at a time
    struct swarmkv *ref_db;
};

struct swarmkv_monitor *module2monitor(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "monitor"));
	struct swarmkv_monitor *monitor=container_of(module, struct swarmkv_monitor, module);
	assert(monitor==module->mod_ctx);
	return monitor;
}
struct swarmkv_module *swarmkv_monitor_new(const struct swarmkv_options *opts, struct swarmkv *db)
{
    struct swarmkv_monitor *monitor=ALLOC(struct swarmkv_monitor, 1);
    monitor->max_latency_usec=opts->cluster_timeout_us;
    monitor->nr_worker_threads=opts->nr_worker_threads;
    monitor->peers=ALLOC(struct recorder *, monitor->nr_worker_threads);
    strncpy(monitor->module.name, "monitor", sizeof(monitor->module.name));
    monitor->module.mod_ctx=monitor;
    pthread_mutex_init(&monitor->latency_execute_mutex, NULL);
    monitor->threads=ALLOC(struct swarmkv_monitor_thread, monitor->nr_worker_threads);
    for(size_t i=0; i<monitor->nr_worker_threads; i++)
    {
        pthread_mutex_init(&monitor->threads[i].mutex, PTHREAD_PROCESS_PRIVATE);
        monitor->threads[i].clients=NULL;
    }
    monitor->ref_db=db;
    return &monitor->module;
}
void swarmkv_monitor_register_command(struct swarmkv_module *mod_monitor, const char *command)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    struct recorder *recorder=NULL;
    recorder=recorder_new(command, monitor->max_latency_usec);
    HASH_ADD_KEYPTR(hh, monitor->commands, recorder->key, strlen(recorder->key), recorder);
    return;
}
void swarmkv_monitor_register_event(struct swarmkv_module *mod_monitor, const char *event)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    struct recorder *recorder=NULL;
    recorder=recorder_new(event, monitor->max_latency_usec);
    HASH_ADD_KEYPTR(hh, monitor->events, recorder->key, strlen(recorder->key), recorder);
    return;
}
void swarmkv_monitor_free(struct swarmkv_module *mod_monitor)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    recorder_table_free(&monitor->commands);
    recorder_table_free(&monitor->events);
    for(size_t i=0; i<monitor->nr_worker_threads; i++)
    {
        recorder_table_free(monitor->peers+i);
        struct monitor_client *client=NULL, *tmp=NULL;
        LL_FOREACH_SAFE(monitor->threads[i].clients, client, tmp)
        {
            LL_DELETE(monitor->threads[i].clients, client);
            monitor_client_free(client);
        }
    }
    free(monitor->threads);
    free(monitor->peers);
    monitor->peers=NULL;
    free(monitor);
}
struct print_ctx
{
    node_t node;
    int tid;
    struct swarmkv_monitor *monitor;
};
static void print_on_reply(const struct swarmkv_reply *reply, void *user)
{
    struct print_ctx *ctx=(struct print_ctx *)user;
    
    if(reply->type != SWARMKV_REPLY_STATUS)
    {
        struct monitor_client *client=NULL, *tmp=NULL;
        pthread_mutex_lock(&ctx->monitor->threads[ctx->tid].mutex);
        LL_FOREACH_SAFE(ctx->monitor->threads[ctx->tid].clients, client, tmp)
        {
            if(0==node_compare(&client->node, &ctx->node))
            {
                ctx->monitor->threads[ctx->tid].n_client--;
                LL_DELETE(ctx->monitor->threads[ctx->tid].clients, client);
                monitor_client_free(client);
            }
        }
        pthread_mutex_unlock(&ctx->monitor->threads[ctx->tid].mutex);
    }
    else
    {
        int tid=swarmkv_gettid(ctx->monitor->ref_db);
        assert(tid==ctx->tid);
    }
    free(ctx);
}
static const char *exec_ret2string(enum cmd_exec_result ret)
{
	switch(ret)
	{
		case NEED_KEY_ROUTE:
			return "KRT";
		case REDIRECT:
			return "RDR";
		case FINISHED:
			return "FIN";
		default:
			assert(0);
	}
}
void swarmkv_monitor_record_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd_spec *spec, const struct swarmkv_cmd *cmd, long long latency_usec, enum cmd_exec_result result)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    recorder_table_record_latency(&monitor->commands, spec->name, strlen(spec->name), latency_usec, monitor->max_latency_usec, 0);
    int tid=swarmkv_gettid(monitor->ref_db);
    assert(tid<monitor->nr_worker_threads);
    if(0==strncasecmp(spec->name, "PRINT", strlen("PRINT")))
    {
        //Do not log print command to avoid infinit recursion.
        return;
    }
    if(monitor->threads[tid].n_client == 0)
    {
        //No monitor client registered.
        return;
    }

    struct monitor_client *client=NULL;
    int matched_client=0;
    node_t matched_client_nodes[monitor->threads[tid].n_client];
    pthread_mutex_lock(&monitor->threads[tid].mutex);
    LL_FOREACH(monitor->threads[tid].clients, client)
    {
        if(client->pattern)
        {
            int is_matched=0;
            for(int i=0; i<cmd->argc; i++)
            {
                is_matched=stringmatchlen(client->pattern, sdslen(client->pattern), cmd->argv[i], sdslen(cmd->argv[i]), 0);
                if(is_matched)
                {
                    break;
                }
            }
            if(!is_matched)
            {
                continue;
            }
        }
        node_copy(matched_client_nodes+matched_client, &client->node);
        matched_client++;
    }
    //To avoid dead lock, we should not hold the lock when executing command.
    pthread_mutex_unlock(&monitor->threads[tid].mutex);
    assert(matched_client <= monitor->threads[tid].n_client);
    if(matched_client==0)
    {
        return;
    }
    
    sds cmdrepr = sdsempty();
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cmdrepr = sdscatprintf(cmdrepr,"%ld.%06ld ",(long)tv.tv_sec, (long)tv.tv_usec);
    cmdrepr = sdscatprintf(cmdrepr,"[%d %c %s %s] ", tid,
                0==node_compare(&cmd->caller, swarmkv_self_node(monitor->ref_db))?'L':'R',
                node_addr2cstr(&cmd->caller),
                exec_ret2string(result));
    for(int i=0; i<cmd->argc; i++)
    {
        cmdrepr = sdscatrepr(cmdrepr, (char*)cmd->argv[i], sdslen(cmd->argv[i]));
        if (i != cmd->argc-1)
            cmdrepr = sdscatlen(cmdrepr," ",1);
    }
    cmdrepr = sdscatprintf(cmdrepr, "\r\n");
    
    const char *print_cmd_argv[2];
    size_t print_cmd_argv_len[2];
    print_cmd_argv[0]="PRINT";
    print_cmd_argv_len[0]=strlen(print_cmd_argv[0]);
    print_cmd_argv[1]=cmdrepr;
    print_cmd_argv_len[1]=sdslen(cmdrepr);

    for(int i=0; i<matched_client; i++)
    {
        struct print_ctx *print_ctx=ALLOC(struct print_ctx, 1);
        node_copy(&print_ctx->node, &matched_client_nodes[i]);
        print_ctx->tid=tid;
        print_ctx->monitor=monitor;
        swarmkv_async_command_on_argv(monitor->ref_db, print_on_reply, print_ctx, print_ctx->node.addr, 2, print_cmd_argv, print_cmd_argv_len);
    }
    sdsfree(cmdrepr);
    return;
}
void swarmkv_monitor_record_peer(struct swarmkv_module *mod_monitor, node_t *peer, long long latency_usec, int thread_id)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    assert(thread_id<monitor->nr_worker_threads);
    recorder_table_record_latency(monitor->peers+thread_id, peer->addr, strlen(peer->addr), latency_usec, monitor->max_latency_usec, 1);
    return;
}
void swarmkv_monitor_record_event(struct swarmkv_module *mod_monitor, const char *event_name, long long latency_usec)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    recorder_table_record_latency(&monitor->events, event_name, strlen(event_name), latency_usec, monitor->max_latency_usec, 0);
    return;
}
struct swarmkv_reply *latency_generic(struct recorder **table, const char *key)
{
    struct swarmkv_reply *reply=NULL;
    struct recorder_metric metric;
    struct recorder_metric *metrics=NULL;
    size_t n_metric=0;
    int ret=0;
    if(key)
    {
        ret=recorder_table_sample_key(table, key, &metric);
        if(ret)
        {
            reply=swarmkv_reply_new_array(1);
            reply->elements[0]=recorder_metric_to_reply(&metric);
        }
        else
        {
            reply=swarmkv_reply_new_array(0);
        }
    }
    else
    {
        recorder_table_sample_all(table, &metrics, &n_metric);
        reply=swarmkv_reply_new_array(n_metric);
        for(size_t i=0; i<n_metric; i++)
        {
            reply->elements[i]=recorder_metric_to_reply(metrics+i);
        }
        free(metrics);
    }
    return reply;
}
enum cmd_exec_result latency_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    pthread_mutex_lock(&monitor->latency_execute_mutex);
	if(cmd->argc==2 && !strcasecmp(cmd->argv[1], "help") )
	{
		const char *help = {
"LATENCY <subcommand> [<arg> [value] [opt] ...]. Subcommands are:\n"
"COMMAND [command]\n"
"    Return time-latency samples for a specified command name.\n"
"PEER [IP:port]\n"
"    Return time-latency samples for the specified peer.\n"
"EVENT [event]\n"
"    Return time-latency samples for the specified event.\n"
"RESET [command|event|peer]\n"
"    Reset data of a specified catalog or all the data if no catalog provided.\n"
		};
		*reply=swarmkv_reply_new_status(help);
	}
    else if(!strcasecmp(cmd->argv[1], "command"))
    {
         *reply=latency_generic(&monitor->commands, cmd->argc>2?cmd->argv[2]:NULL);
    }
    else if(!strcasecmp(cmd->argv[1], "event"))
    {
        *reply=latency_generic(&monitor->events, cmd->argc>2?cmd->argv[2]:NULL);
    }
    else if(!strcasecmp(cmd->argv[1], "peer"))
    {
        struct recorder *merged_table=NULL;
        //Dirty code: Reading all worker threads' peer recorder tables is not thread-safe. 
        //It's OK only if the worker threads are not adding new peers.
        recorder_table_merge(monitor->peers, monitor->nr_worker_threads, &merged_table);
        *reply=latency_generic(&merged_table, cmd->argc>2?cmd->argv[2]:NULL);
        recorder_table_free(&merged_table);
    }
    else if(!strcasecmp(cmd->argv[1], "reset"))
    {
        if(cmd->argc>2)
        {
            if(!strcasecmp(cmd->argv[2], "command"))
            {
                recorder_table_reset(&monitor->commands);
                *reply=swarmkv_reply_new_status("Command metrics are reset");
            }
            else if(!strcasecmp(cmd->argv[2], "event"))
            {
                recorder_table_reset(&monitor->events);
                *reply=swarmkv_reply_new_status("Event metrics are reset");
            }
            else if(!strcasecmp(cmd->argv[2], "peer"))
            {
                for(size_t i=0; i<monitor->nr_worker_threads; i++)
                {
                    recorder_table_reset(monitor->peers+i);
                }
                *reply=swarmkv_reply_new_status("Peer metrics are rest");
            }
            else
            {
                *reply=swarmkv_reply_new_error(erorr_subcommand_syntax, cmd->argv[1], cmd->argv[0]);
            }
        }
        else
        {
            recorder_table_reset(&monitor->commands);
            recorder_table_reset(&monitor->events);
            for(size_t i=0; i<monitor->nr_worker_threads; i++)
            {
                recorder_table_reset(monitor->peers+i);
            }
            *reply=swarmkv_reply_new_status("All metrics are reset");
        }
    }
    else
    {
        *reply=swarmkv_reply_new_error(erorr_subcommand_syntax, cmd->argv[1], cmd->argv[0]);
    }
    pthread_mutex_unlock(&monitor->latency_execute_mutex);
    return FINISHED;
}
enum cmd_exec_result monreg_command(struct swarmkv_module *mod_monitor, const struct swarmkv_cmd *cmd, struct swarmkv_reply **reply)
{
/* MONREG IP:port [pattern]*/
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    node_t node;
    int ret=0;
    ret=node_init_from_sds(&node, cmd->argv[1]);
    if(ret<0)
    {
        *reply=swarmkv_reply_new_error(error_arg_not_valid_node);
        return FINISHED;
    }
    for(int i=0; i<monitor->nr_worker_threads; i++)
    {
        struct monitor_client *client=NULL;
        int found=0;
        pthread_mutex_lock(&monitor->threads[i].mutex);
        LL_FOREACH(monitor->threads[i].clients, client)
        {
            if(0==node_compare(&client->node, &node))
            {
                sdsfree(client->pattern);
                client->pattern=cmd->argc>2?sdsdup(cmd->argv[2]):NULL;            
                found=1;
            }
        }
        if(!found)
        {
            client=monitor_client_new(&node, cmd->argc>2?cmd->argv[2]:NULL);
            LL_APPEND(monitor->threads[i].clients, client);
            monitor->threads[i].n_client++;
        }
        pthread_mutex_unlock(&monitor->threads[i].mutex);
    }
    *reply=swarmkv_reply_new_status("OK");
    return FINISHED;
}