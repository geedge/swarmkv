#pragma once
#include <sys/time.h>
#ifdef __cplusplus
extern "C"{
#endif

enum e_future_error
{
	FUTURE_ERROR_CANCEL,
	FUTURE_ERROR_EXCEPTION,
	FUTURE_ERROR_TIMEOUT
};

struct promise;
struct future;
typedef void (future_success_cb)(const void *result, void * user);
typedef void (future_failed_cb)(enum e_future_error err, const char * what, void * user);
typedef void (promise_ctx_destroy_cb)(void* ctx);


/*
* Future APIs are used by Async RPC caller.
*/
struct future * future_create(const char* symbol, future_success_cb * cb_success, future_failed_cb * cb_failed, void * user);
void future_set_timeout(struct future * f, struct timeval timeout);
void future_destroy(struct future * f);


/*
* Promise APIs  are used by Async RPC implementation.
*/
struct promise * future_to_promise(struct future * f);
void promise_failed(struct promise * p, enum e_future_error error, const char * what);
void promise_success(struct promise * p, const void * result);
void promise_finish(struct promise * p);
void promise_allow_many_successes(struct promise *p);

void promise_set_ctx(struct promise * p, void * ctx, promise_ctx_destroy_cb * cb);
void * promise_get_ctx(struct promise * p);
void * promise_dettach_ctx(struct promise * p);
//return 1 on a meaningful timeout, or 0 on no timeout.
int promise_get_timeout(struct promise * p, struct timeval * timeout);

#ifdef __cplusplus 
}//end extern "C"
#endif

