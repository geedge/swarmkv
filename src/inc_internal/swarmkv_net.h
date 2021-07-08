#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h> 
#include <net/if.h>  
#include <sys/ioctl.h> 

char* get_local_ip(const char *eth_inf);
int swarmkv_create_socket(int port);
int swarmkv_msg_send(int sockfd, char* msg, char* peer_ip, uint16_t peer_port);