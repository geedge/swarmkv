#include "swarmkv/swarmkv.h"
#include "stdlib.h"
#include "stdio.h"
int main(int argc, char **argv)
{
    struct swarmkv_options *opts[2];
    struct swarmkv *db[2];
    char *err=NULL;
    const char *cluster_name="simple-example";
    for(size_t i=0; i<2; i++)
    {
        opts[i]=swarmkv_options_new();
        swarmkv_options_set_p2p_port(opts[i], 5210+i);
        db[i]=swarmkv_open(opts[i], cluster_name, &err);
        if(err)
        {
            printf("swarmkv_open failed: %s.\n", err);
            free(err);
            return -1;
        }
    }
    for(size_t i=0; i<2; i++)
    {
	    swarmkv_close(db[i]);
    }
}