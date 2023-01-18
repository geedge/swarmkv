#include "swarmkv/swarmkv.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

struct my_context
{
    int got_reply;
    long long reply_integer;
};
void my_on_reply_callback(const struct swarmkv_reply *reply, void * arg)
{
    struct my_context *ctx=(struct my_context *)arg;
    ctx->got_reply=1;
    ctx->reply_integer=reply->integer;
    return;
}
int main(int argc, char **argv)
{
    struct swarmkv_options *opts[2];
    struct swarmkv *db[2];
    char *err=NULL;
    const char *cluster_name="simple-example";
    for(size_t i=0; i<2; i++)
    {
        opts[i]=swarmkv_options_new();
        swarmkv_options_set_cluster_port(opts[i], 5210+i);
        db[i]=swarmkv_open(opts[i], cluster_name, &err);
        if(err)
        {
            printf("swarmkv_open failed: %s.\n", err);
            free(err);
            return -1;
        }
    }
    struct my_context *ctx=NULL;
    ctx=(struct my_context*)calloc(sizeof(struct my_context), 1);
    swarmkv_async_command(db[0], my_on_reply_callback, ctx, "HSET uid001 name zhangsan age 18");
    
    while(!ctx->got_reply)
    {
        //do something else
        sleep(1);
    }
    printf("HSET %lld items\n", ctx->reply_integer);
    free(ctx);
    for(size_t i=0; i<2; i++)
    {
	    swarmkv_close(db[i]);
    }
}