#include "uthash.h"
#include "sds.h"
#include "log.h"
#include "uthash.h"
#include "swarmkv_monitor.h"
#include "swarmkv_common.h"
#include "swarmkv_utils.h"
#include "swarmkv_error.h"
#include <hdr/hdr_histogram.h>
#include <hdr/hdr_interval_recorder.h>

#include <pthread.h>
#define METRIC_KEY_MAX  64
#define SIGNIFICANT_FIGURES 2
#define LOWEST_TRACKABLE_VALUE  1
struct recorder
{
    char key[METRIC_KEY_MAX];
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
    char key[METRIC_KEY_MAX];
    long long total_count;
    long long max;
    long long min;
    double stddev, mean;
    long long p50, p80, p90, p95, p99;
    double rate;
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
                                    "count=%lld "
                                    "rate=%.2f"
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
                                    metric->total_count,
                                    metric->rate
                                    );
}
void recorder_sample(struct recorder *recorder, struct recorder_metric *metric)
{
    memset(metric, 0, sizeof(struct recorder_metric));
    recorder->hdr_previous=hdr_interval_recorder_sample_and_recycle(&recorder->hdr_interval, recorder->hdr_previous);
    long long recent_total_count=recorder->hdr_previous->total_count;
    long long __attribute__((__unused__))dropped=hdr_add(recorder->hdr_all_time, recorder->hdr_previous);
    assert(dropped==0);
    strncpy(metric->key, recorder->key, sizeof(metric->key));
    metric->total_count=recorder->hdr_all_time->total_count;
    if(metric->total_count==0) return;
    metric->p50=hdr_value_at_percentile(recorder->hdr_all_time, 0.5);
    metric->p80=hdr_value_at_percentile(recorder->hdr_all_time, 0.8);
    metric->p90=hdr_value_at_percentile(recorder->hdr_all_time, 0.9);
    metric->p95=hdr_value_at_percentile(recorder->hdr_all_time, 0.95);
    metric->p99=hdr_value_at_percentile(recorder->hdr_all_time, 0.99);
    metric->total_count=recorder->hdr_all_time->total_count;
    metric->max=hdr_max(recorder->hdr_all_time);
    metric->mean=hdr_mean(recorder->hdr_all_time);
    metric->min=hdr_min(recorder->hdr_all_time);
    metric->stddev=hdr_stddev(recorder->hdr_all_time);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double diff_time_ms=(now.tv_sec-recorder->last_sampled.tv_sec)*1000+(now.tv_nsec-recorder->last_sampled.tv_nsec)/1000/1000;
    diff_time_ms=MAX(diff_time_ms, 1);
    metric->rate=(double)(recent_total_count)*1000/diff_time_ms;
    clock_gettime(CLOCK_MONOTONIC, &(recorder->last_sampled));
    return;
}
void recorder_reset(struct recorder *recorder)
{
    recorder->hdr_previous=hdr_interval_recorder_sample_and_recycle(&recorder->hdr_interval, recorder->hdr_previous);
    hdr_reset(recorder->hdr_previous);
    hdr_reset(recorder->hdr_all_time);
    clock_gettime(CLOCK_MONOTONIC, &(recorder->last_sampled));
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
struct swarmkv_reply *recorder_metric_to_reply(const struct recorder_metric *metric)
{
    char metric_buff[1024];
    metric2string(metric, metric_buff, sizeof(metric_buff));
    struct swarmkv_reply *reply=swarmkv_reply_new_array(2);
    reply->elements[0]=swarmkv_reply_new_string_fmt(metric->key);
    reply->elements[1]=swarmkv_reply_new_string_fmt(metric_buff);
    return reply;
}
struct swarmkv_monitor
{
    struct swarmkv_module module;
    struct recorder *commands;
    pthread_mutex_t lock_event_recorder;
    struct recorder *events;
    struct recorder **peers;
    long long max_latency_usec;
    int significant_figures;
    int nr_worker_threads;
};

struct swarmkv_monitor *module2monitor(struct swarmkv_module *module)
{
	assert(0==strcmp(module->name, "monitor"));
	struct swarmkv_monitor *monitor=container_of(module, struct swarmkv_monitor, module);
	assert(monitor==module->mod_ctx);
	return monitor;
}
struct swarmkv_module *swarmkv_monitor_new(const struct swarmkv_options *opts)
{
    struct swarmkv_monitor *monitor=ALLOC(struct swarmkv_monitor, 1);
    monitor->max_latency_usec=opts->cluster_timeout_us;
    monitor->nr_worker_threads=opts->nr_worker_threads;
    monitor->peers=ALLOC(struct recorder *, monitor->nr_worker_threads);
    pthread_mutex_init(&monitor->lock_event_recorder, NULL);
    strncpy(monitor->module.name, "monitor", sizeof(monitor->module.name));
    monitor->module.mod_ctx=monitor;
    return &monitor->module;
}
void swarmkv_monitor_register_commands(struct swarmkv_module *mod_monitor, const char *command)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    struct recorder *recorder=NULL;
    recorder=recorder_new(command, monitor->max_latency_usec);
    HASH_ADD_KEYPTR(hh, monitor->commands, recorder->key, strlen(recorder->key), recorder);
    return;
}
void swarmkv_monitor_free(struct swarmkv_module *mod_monitor)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    struct recorder *recorder=NULL, *tmp=NULL;
	HASH_ITER(hh, monitor->commands, recorder, tmp)
	{
        HASH_DEL(monitor->commands, recorder);
        recorder_free(recorder);
	}
	HASH_ITER(hh, monitor->events, recorder, tmp)
	{
        HASH_DEL(monitor->events, recorder);
        recorder_free(recorder);
	}
    for(size_t i=0; i<monitor->nr_worker_threads; i++)
    {
        HASH_ITER(hh, monitor->peers[i], recorder, tmp)
        {
            HASH_DEL(monitor->peers[i], recorder);
            recorder_free(recorder);
        }   
    }
    free(monitor->peers);
    monitor->peers=NULL;
    free(monitor);
}
void swarmkv_monitor_record_command(struct swarmkv_module *mod_monitor, const char *cmd_name, long long latency_usec)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    recorder_table_record_latency(&monitor->commands, cmd_name, strlen(cmd_name), latency_usec, monitor->max_latency_usec, 0);
    return;
}
void swarmkv_monitor_record_peer(struct swarmkv_module *mod_monitor, node_t *peer, long long latency_usec, int thread_id)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);
    recorder_table_record_latency(monitor->peers+thread_id, peer->addr, strlen(peer->addr), latency_usec, monitor->max_latency_usec, 1);
    return;
}
void swarmkv_monitor_record_event(struct swarmkv_module *mod_monitor, const char *event_name, long long latency_usec)
{
    struct swarmkv_monitor *monitor=module2monitor(mod_monitor);

    pthread_mutex_lock(&monitor->lock_event_recorder);
    recorder_table_record_latency(&monitor->events, event_name, strlen(event_name), latency_usec, monitor->max_latency_usec, 1);
    pthread_mutex_unlock(&monitor->lock_event_recorder);
    return;
}
struct swarmkv_reply *lantency_generic(struct recorder **table, const char *key)
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
         *reply=lantency_generic(&monitor->commands, cmd->argc>2?cmd->argv[2]:NULL);
    }
    else if(!strcasecmp(cmd->argv[1], "event"))
    {
        pthread_mutex_lock(&monitor->lock_event_recorder);
        *reply=lantency_generic(&monitor->events, cmd->argc>2?cmd->argv[2]:NULL);
        pthread_mutex_unlock(&monitor->lock_event_recorder);        
    }
    else if(!strcasecmp(cmd->argv[1], "peer"))
    {
        struct swarmkv_reply *tmp=NULL;
        for(size_t i=0; i<monitor->nr_worker_threads; i++)
        {
            tmp=lantency_generic(monitor->peers+i, cmd->argc>2?cmd->argv[2]:NULL);
            swarmkv_reply_merge_array(reply, tmp);
        }
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
    
    return FINISHED;
}