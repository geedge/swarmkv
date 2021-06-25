#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<arpa/inet.h>

#include "swarmkv.c"


#define BUFSIZE 2048
#define PORT 8323
int create_socket(struct swarmkv_store* store)
{
    int sockfd;
    char recvbuf[BUFSIZ];
    struct sockaddr_in my_addr;
    memset(&my_addr,0,sizeof(my_addr));
    my_addr.sin_family=AF_INET; 
    my_addr.sin_port=htons(PORT); 
    my_addr.sin_addr.s_addr=htonl(INADDR_ANY);//服务器IP地址--允许连接到所有本地地址上

    if((sockfd=socket(PF_INET, SOCK_DGRAM, 0))<0)   //create socket
    {  
        perror("socket");
        return 0;
    }
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))<0)   //bind socket to the port
    {
        perror("bind");
        return 0;
    }
    struct sockaddr_in peer_addr;
    socklen_t peerlen=sizeof(struct sockaddr_in);
    int n;
    struct swarmkv_node* peer_nodes = store->nodes;
    addr.sin_addr.s_addr = inet_addr("x.x.x.x");

    while(1)
	{
        memset(recvbuf, 0, sizeof(recvbuf));
		n = recvfrom(sockfd, recvbuf, 127, 0, (struct sockaddr *)&peer_addr, &peerlen);
		if(n <= 0)
		{
			continue;
		}
        else
        {
            //printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
		    //sendto(sockfd,"ok",2,0,(struct sockaddr*)&peer_addr,sizeof(peer_addr));
        }	
	}
	
    return 1;
}