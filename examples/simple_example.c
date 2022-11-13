#include "swarmkv/swarmkv.h"
#include "stdlib.h"
int main(int argc, char **argv)
{
    struct swarmkv_options *opts=swarmkv_options_new();
    swarmkv_options_set_p2p_port(opts, 5210);
 
    struct swarmkv *db=NULL;
    char *err=NULL;
    const char *cluster_name="simple-example";
    db=swarmkv_open(opts, cluster_name, &err);
    if(err)
    {
        printf("swarmkv_open failed: %s.\n", err);
        free(err);
        return -1;
    }
    const char *key="name";
    const char *value="zhangsan";
    struct swarmkv_reply *reply=NULL;
    reply=swarmkv_command(db, "set %s %s", key, value);
    swarmkv_reply_free(reply);
    reply=swarmkv_command(db, "get %s", key);
    if(reply->type==SWARMKV_REPLY_STRING)
    {
        printf("get name: %s\n", reply->str);
    }
    else if(reply->type==SWARMKV_REPLY_NIL)
    {
        printf("get name: NULL\n");
    }
    else if(reply->type==SWARMKV_REPLY_ERROR)
    {
         printf("get name error: %s\n", reply->str);
    }
    swarmkv_reply_free(reply);
	swarmkv_close(db);
    return 0;
}