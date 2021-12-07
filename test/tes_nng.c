#include<stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "../src/inc_internal/swarmkv_nng.h"

int main(int argc, char **argv)
{
    //nng_listen();
    char* url = "tcp://172.16.225.2:8323";
    int sockfd = nng_create_socket_to_connect(url);
    //const char* msg = "{\"my_id\":0,\"my_ip\":\"172.16.225.2\",\"my_port\":8323}";
    const char* msg = "hello";
    swarmkv_msg_nng_send(sockfd, msg, strlen(msg));
    //const char* msg2 = "hi";
    //swarmkv_msg_nng_send(sockfd, msg2, strlen(msg2));
    char nng_buf[200] = {0};
    while(1)
    {
        //阻塞读取
        int rc = nn_recv(sockfd, nng_buf, 200, 0);
        if (rc < 0)
        {
            printf("nn_recv failed! error\n");
            continue;
        }
        else
        {
            printf("recieve client msg: %s\r\n", nng_buf);
        }
    }
    nn_close(sockfd);
}