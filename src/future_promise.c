#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "future_promise.h"
#include "swarmkv_utils.h"


#define	FUTURE_SYMBOL_MAX	128

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
};

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
void promise_failed(struct promise * p, enum e_future_error error, const char * what)
{
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

