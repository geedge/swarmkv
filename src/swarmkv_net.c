//#include "./inc_internal/swarmkv_net.h"
#include "swarmkv_net.h"

char* get_local_ip(const char *eth_inf)
{
    int sd;
    struct sockaddr_in sin;
    struct ifreq ifr;
 
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == sd)
    {
        printf("socket error: %s\n", strerror(errno));
        return NULL;
    }
    strncpy(ifr.ifr_name, eth_inf, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    if (ioctl(sd, SIOCGIFADDR, &ifr) < 0)
    {
        printf("ioctl error: %s\n", strerror(errno));
        close(sd);
        return NULL;
    }
    //printf("interfac: %s, ip: %s\n", eth_inf, inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr)); 
	char* local_ip = inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr);
    close(sd);
	//printf("local_ip: %s\n",local_ip);
    return local_ip;
}

int swarmkv_create_socket(int port)
{
	int sockfd;
	char recvbuf[BUFSIZ];
	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		perror("socket");
		return 0;
	}
	int on=1;
  	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_BROADCAST, &on, sizeof(on));

	//set socket to non-block
	fcntl(sockfd, F_SETFL, O_NONBLOCK);

	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) < 0)
	{
		perror("bind");
		return 0;
	}
	return sockfd;
}

int swarmkv_msg_send(int sockfd, char* msg, char* peer_ip, uint16_t peer_port)
{
    struct sockaddr_in peer_addr;
	memset(&peer_addr,0,sizeof(peer_addr));
	peer_addr.sin_family = AF_INET;
	peer_addr.sin_port = htons(peer_port);
	peer_addr.sin_addr.s_addr = inet_addr(peer_ip);

    if(sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) 
    {
		printf("snedto error: %s\n", strerror(errno));
        return -1;
    }
    return 1;
}