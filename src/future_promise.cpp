#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "future_promise.h"
#include "swarmkv_utils.h"
#include "uthash.h"
#include <MESA/field_stat2.h>

#define	FUTURE_SYMBOL_MAX	128

static const char* FP_HISTOGRAM_BINS="0.50,0.80,0.9,0.95,0.99";

struct symbol_fs_info
{
	char symbol[FUTURE_SYMBOL_MAX];
	int fsid_latency;
	int fsid_failed;
	UT_hash_handle hh;
};
struct future_promise_instance
{
	int fsid_f_num;
	long long f_num;
	int no_stats;
	int statsd_cycle;
	int statsd_format;
	struct symbol_fs_info* symbol2fsid;
	char statsd_server_ip[256];
	int statsd_server_port;
	enum field_calc_algo favorite;
	char histogram_bins[256];
	screen_stat_handle_t fs_handle;
};

struct _future_promise_debug
{
	int fsid_latency;
	int fsid_failed;
	long long succ_times;
	struct timespec create_time;
};
struct future
{
	void * user;
	char symbol[FUTURE_SYMBOL_MAX];
	struct timeval timeout;
	future_success_cb * cb_success;
	future_failed_cb * cb_failed;
	char is_canceled;
};

struct promise
{
	struct future f;
	void * ctx;
	char has_timeout;
	char ref_cnt;
	char may_success_many_times;
	promise_ctx_destroy_cb * cb_ctx_destroy;
	struct _future_promise_debug debug;
};
static struct future_promise_instance g_FP_instance;
static int g_is_FP_init=0;
void future_promise_library_init(const char* profile)
{
	if(g_is_FP_init==1)
	{
		return;
	}

	int value=0;
	memset(&g_FP_instance,0,sizeof(g_FP_instance));
	g_FP_instance.favorite=FS_CALC_CURRENT;
	strcpy(g_FP_instance.histogram_bins, FP_HISTOGRAM_BINS);
	if(g_FP_instance.no_stats)
	{
		g_is_FP_init=1;
		return;
	}

	if (g_FP_instance.statsd_format != 1 && g_FP_instance.statsd_format != 2)
	{
		g_FP_instance.statsd_format = 1;
	}

	screen_stat_handle_t fs=NULL;
	const char* stat_path="future.fs2";
	const char* app_name="FP";
	fs=FS_create_handle();
	FS_set_para(fs, APP_NAME, app_name, strlen(app_name)+1);
	value=1;
	FS_set_para(fs, OUTPUT_PROMETHEUS, &value, sizeof(value));
	value=0;
	FS_set_para(fs, FLUSH_BY_DATE, &value, sizeof(value));
	FS_set_para(fs, OUTPUT_DEVICE, stat_path, strlen(stat_path)+1);
	value=1;
	FS_set_para(fs, PRINT_MODE, &value, sizeof(value));
	value=1;
	FS_set_para(fs, CREATE_THREAD, &value, sizeof(value));
	FS_set_para(fs, STAT_CYCLE, &g_FP_instance.statsd_cycle, sizeof(g_FP_instance.statsd_cycle));
	if(strlen(g_FP_instance.statsd_server_ip)>0 && g_FP_instance.statsd_server_port!=0)
	{
		FS_set_para(fs, STATS_SERVER_IP, g_FP_instance.statsd_server_ip, strlen(g_FP_instance.statsd_server_ip)+1);
		FS_set_para(fs, STATS_SERVER_PORT, &(g_FP_instance.statsd_server_port), sizeof(g_FP_instance.statsd_server_port));
		FS_set_para(fs, STATS_FORMAT, &(g_FP_instance.statsd_format), sizeof(g_FP_instance.statsd_format));
	}
	FS_set_para(fs, HISTOGRAM_GLOBAL_BINS, g_FP_instance.histogram_bins, strlen(g_FP_instance.histogram_bins)+1);
	g_FP_instance.fsid_f_num=FS_register(fs, FS_STYLE_FIELD, g_FP_instance.favorite, "futures");
	FS_start(fs);
	g_FP_instance.fs_handle=fs;
	g_is_FP_init=1;
	return;
}
static struct promise * __future_to_promise(struct future * f)
{
	return (struct promise *) f;
}
static void __promise_destroy(struct promise *p)
{
	if (p->cb_ctx_destroy != NULL)
	{
		p->cb_ctx_destroy(p->ctx);
	}
	if(!g_FP_instance.no_stats)	FS_operate(g_FP_instance.fs_handle,g_FP_instance.fsid_f_num, 0, FS_OP_SUB, 1);
	memset(p, 0, sizeof(struct promise));
	free(p);
	return;
}

struct promise * future_to_promise(struct future * f)
{
	if(f==NULL)
	{
		return NULL;
	}
	struct promise *p=__future_to_promise(f);
	p->ref_cnt++;
	assert(p->ref_cnt==2);
	return (struct promise *) f;
}
void promise_allow_many_successes(struct promise *p)
{
	p->may_success_many_times=1;
	return;
}

struct future * future_create(const char* symbol, future_success_cb * cb_success, future_failed_cb * cb_failed, void * user)
{
	struct promise * p = ALLOC(struct promise, 1);
	p->f.user = user;
	p->f.cb_success = cb_success;
	p->f.cb_failed = cb_failed;
	p->ref_cnt=1;
	strncpy(p->f.symbol, symbol, sizeof(p->f.symbol));
	char buff[FUTURE_SYMBOL_MAX+32]="";
	struct symbol_fs_info* fs_info=NULL;
	if(!g_FP_instance.no_stats)
	{
		clock_gettime(CLOCK_MONOTONIC, &p->debug.create_time);
		HASH_FIND(hh, g_FP_instance.symbol2fsid, symbol, strlen(symbol), fs_info);
		if(!fs_info)
		{
			fs_info=ALLOC(struct symbol_fs_info, 1);
			strncpy(fs_info->symbol, symbol, sizeof(fs_info->symbol));			
			snprintf(buff, sizeof(buff), "%s(ms)", fs_info->symbol);
			fs_info->fsid_latency=FS_register_histogram(g_FP_instance.fs_handle, g_FP_instance.favorite, buff,
										1, 30*1000, 3);
			snprintf(buff, sizeof(buff), "%s_fail", fs_info->symbol);
			fs_info->fsid_failed=FS_register(g_FP_instance.fs_handle, FS_STYLE_FIELD, g_FP_instance.favorite, buff);
			HASH_ADD(hh, g_FP_instance.symbol2fsid, symbol, strlen(symbol), fs_info);
		}
		p->debug.fsid_latency=fs_info->fsid_latency;
		p->debug.fsid_failed=fs_info->fsid_failed;
		FS_operate(g_FP_instance.fs_handle, g_FP_instance.fsid_f_num, 0, FS_OP_ADD, 1);
	}
	return &p->f;
}
void future_set_timeout(struct future * f, struct timeval timeout)
{
	struct promise * p=(struct promise *) f;
	f->timeout=timeout;
	p->has_timeout=1;
	return;
}

void future_destroy(struct future * f)
{
	struct promise * p = __future_to_promise(f);
	p->ref_cnt--;
	if(p->ref_cnt==0)
	{
		__promise_destroy(p);
	}
	else
	{
		f->is_canceled=1;
	}
}
void promise_finish(struct promise * p)
{
	p->ref_cnt--;
	if(p->ref_cnt==0)
	{
		__promise_destroy(p);
	}
}
static void fp_stat_latency(struct _future_promise_debug* debug, int is_success)
{
	struct timespec end;
	long long jiffies_ms=0;
	clock_gettime(CLOCK_MONOTONIC,&end);
	if(is_success==1)
	{
		debug->succ_times++;
	}
	else
	{
		FS_operate(g_FP_instance.fs_handle, debug->fsid_failed, 0, FS_OP_ADD, 1);
	}
	if(debug->succ_times<=1)
	{
		jiffies_ms=(end.tv_sec-debug->create_time.tv_sec)*1000+(end.tv_nsec-debug->create_time.tv_nsec)/1000000;
		FS_operate(g_FP_instance.fs_handle, debug->fsid_latency, 0, FS_OP_SET, jiffies_ms);
	}
	return;
}
void promise_failed(struct promise * p, enum e_future_error error, const char * what)
{
	if(!g_FP_instance.no_stats) fp_stat_latency(&p->debug, 0);
	if(!p->f.is_canceled)
	{
		p->f.cb_failed(error, what, p->f.user);
	}
	if(!p->may_success_many_times)
	{
		promise_finish(p);
	}
	return;
}

void promise_success(struct promise * p, void * result)
{
	if(!g_FP_instance.no_stats) fp_stat_latency(&p->debug, 1);
	if(!p->f.is_canceled)
	{
		p->f.cb_success(result, p->f.user);
	}
	if(!p->may_success_many_times)
	{
		promise_finish(p);
	}
	return;
}

void promise_set_ctx(struct promise * p, void * ctx, promise_ctx_destroy_cb * cb)
{
	p->ctx = ctx;
	p->cb_ctx_destroy = cb;
	return;
}

void * promise_get_ctx(struct promise * p)
{
	return p->ctx;
}

void * promise_dettach_ctx(struct promise * p)
{
	void * ctx = p->ctx;
	p->ctx = NULL;
	p->cb_ctx_destroy = NULL;
	return ctx;
}
/**
   Get timeout from a promise which is set in future.

   @param timeout Output.
   @return 1 on a meaningful timeout, or 0 on no timeout.
*/
int promise_get_timeout(struct promise * p, struct timeval * timeout)
{
	if(p->has_timeout)
	{
		*timeout=p->f.timeout;
	}
	return p->has_timeout;
}

