#include "swarmkv/swarmkv.h"
#include "stdio.h"
#include "stdlib.h"

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
    const char *key="name";
    const char *value="zhangsan";
    struct swarmkv_reply *reply=NULL;
    reply=swarmkv_command(db[0], "set %s %s", key, value);
    swarmkv_reply_free(reply);
    reply=swarmkv_command(db[1], "get %s", key);
    if(reply->type==SWARMKV_REPLY_STRING)
    {
        printf("get name: %s\n", reply->str);
    }
    else
    {
        printf("get name failed, reply type: %d, str: %s\n",
                    reply->type, reply->str);
    }
    swarmkv_reply_free(reply);
    for(size_t i=0; i<2; i++)
    {
	    swarmkv_close(db[i]);
    }
    return 0;
}