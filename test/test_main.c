#include<stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "../src/inc_internal/swarmkv_nng.h"

int main(int argc, char **argv)
{
    //nng_listen();
    char* url = "tcp://192.168.40.182:8323";
    int sockfd = nng_create_socket_to_listen(url);
    printf("sockfd: %d\n", sockfd);
    while(1)
    {
        //阻塞读取
        char buf[200] = {0};
        int rc = nn_recv(sockfd, &buf, 200, 0);
        if (rc < 0)
        {
            printf("nn_recv failed! error\n");
            continue;
        }
        else
        {
            printf("recieve:\n");
            printf("recieve client msg: %s\r\n",  buf);
            const char* msg = "world";
            swarmkv_msg_nng_send(sockfd, msg, strlen(msg));
        }
        //nn_freemsg (buf);
    }
    nn_close(sockfd);
}