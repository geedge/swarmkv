#include "swarmkv_nng.h"

 //int nn_recv（int s，void * buf，size_t len，int flags）;  套接字/接受数据缓存/最大可接受长度/0阻塞|| 1非阻塞，返回值为接收到的数据长度
 //int nn_send (int s, const void *buf, size_t len, int flags);  套接字/发送的数据缓存/数据长度/阻塞||非阻塞，返回值为发送出去的数据长度
int nng_create_socket_to_listen(const char* url)
{
    int socket = nn_socket(AF_SP, NN_PAIR);
    if ( socket < 0)
    {
        printf("create server socket failed!\n");
        return -1;
    }
    if (nn_bind(socket, url) < 0)
    {
        printf("bind server sock failed!\r\n");
        nn_close(socket);
        return -1;
    }
    printf("server init success!\n");
    return socket;
}

int nng_create_socket_to_connect(const char* url)
{
    int socket = nn_socket(AF_SP, NN_PAIR);
    if ( socket < 0)
    {
        printf("create server socket failed!\n");
        return -1;
    }
    if (nn_connec(socket, url) < 0)
    {
        printf("connect server sock failed!\r\n");
        nn_close(socket);
        return -1;
    }
    printf("server init success!\n");
    return socket;
}

/*
int nng_socket(int nng_socket)
{
    char buf[BUF_LEN] = {0};
    while(1)
    {
        int rc = nn_recv(nng_socket, buf, sizeof(buf), 0);
        if (rc < 0)
        {
            printf("nn_recv failed! error: %s.rc = %d.\n", nn_err_strerror(errno), rc);
            continue;
            //nn_close(nng_socket);
            //exit(EXIT_FAILURE);
        }
        else
        {
            printf("recieve client msg: %s\r\n", buf);
            if (nn_send(nng_socket, buf, sizeof(buf), 0) < 0)
            {
                printf("send failed!\r\n");
                nn_close(nng_socket);
                exit(EXIT_FAILURE);
            }
        }
    }
    nn_close(nng_socket);
    return 0;
}
*/
