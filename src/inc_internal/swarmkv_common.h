#pragma once
#include <netinet/ip.h>

struct swarmkv_node
{
	int node_id;
//	struct sockaddr_in peer_addr;	//man 7 ip
	char ip_addr[INET6_ADDRSTRLEN];	//#define INET6_ADDRSTRLEN 46 (#include <arpa/inet.h>, see man inet_ntop
	unsigned int p2p_port;	//host order
};

