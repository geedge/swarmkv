#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<string.h>
#include<arpa/inet.h>
#include <fcntl.h>

#include "swarmkv.c"

#define BUFSIZE 2048
#define SWARMKV_DEFAULT_LISTEN_PORT 8323

void *thread_fun(int sockfd)
{	
	int sockfd = (int)arg;
    struct sockaddr_in peer_addr;
    socklen_t peerlen = sizeof(struct sockaddr_in);
	//set socket to non_block
	while(1)
	{
		char buff[128] = {0};
        //check cluster nodes status
        for(i=0; i<cluster_node_cnt; i++)
        {
            if(node->ping_sent_time-time()> timeout)
            {
                node->ping_timeout_cnt++;
                if(node->ping_timeout_cnt>MAX_TIMEOUT)
                {
                    node->status==FAIL;
                }
                else
                {
                    send(socket, ping_msg);
                    node->ping_sent_time=time();
                }
            }
        }
		int n = recvfrom(sockfd, recvbuf, 127, 0, (struct sockaddr *)&peer_addr, &peerlen);
		if(n > 0) //
		{
			msg=parse_msg();
            switch(msg->type)
            {
                //handle put, get, ping and etc.
            }
		}
		printf("buff[%d]=%s\n",c,buff);
	}
	printf("one client over\n");
	close(c);
	//pthread_exit(NULL);	
}


int create_socket(struct swarmkv_store* store)
{
    int sockfd;
    char recvbuf[BUFSIZ];
    struct sockaddr_in my_addr;
    memset(&my_addr,0,sizeof(my_addr));
    my_addr.sin_family=AF_INET; 
    my_addr.sin_port=htons(SWARMKV_DEFAULT_LISTEN_PORT); 
    my_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    if((sockfd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))<0)  
    {  
        perror("socket");
        return 0;
    }
    //set socket to non-block
    fcntl(sd, F_SETFL, O_NONBLOCK);  

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))<0)  
    {
        perror("bind");
        return 0;
    }
    return sockfd;

    struct sockaddr_in peer_addr;
    socklen_t peerlen = sizeof(struct sockaddr_in);
    //peer_addr.sin_addr.s_addr = inet_addr("x.x.x.x");
    //struct swarmkv_node* peer_nodes = store->nodes;
    int n;

    while(1)
	{
        //pthread_t id;
		//pthread_create(&id,NULL,thread_fun,(void*)c);
        /*
        memset(recvbuf, 0, sizeof(recvbuf));
		n = recvfrom(sockfd, recvbuf, 127, 0, (struct sockaddr *)&peer_addr, &peerlen);
		if(n <= 0)
		{
			continue;
		}
        else
        {
            printf("port:%d,buff=%s\n",htons(peer_addr.sin_port),recvbuf);
            char resp_buff[128] = {0};
		    printf("response:\n");
		    fgets(resp_buff,128,stdin);
		    sendto(sockfd,resp_buff,128,0,(struct sockaddr*)&peer_addr,sizeof(peer_addr));
        }	
        */
	}
	
    return 1;
}