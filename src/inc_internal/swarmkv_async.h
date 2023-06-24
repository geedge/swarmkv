#include "swarmkv/swarmkv.h"
#include "future_promise.h"
struct swarmkv_async;
struct swarmkv_async *swarmkv_async_new();
void swarmkv_async_free(struct swarmkv_async *async);
//Return a sequence number, which can be used to complete the request
long long swarmkv_async_launch(struct swarmkv_async *async, int thread_id, struct future *f);
void swarmkv_async_complete(struct swarmkv_async *async, int thread_id, long long sequence, const struct swarmkv_reply *reply);
