#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
//#include "nanomsg/pair.h"
//#include "nanomsg/nn.h"
//#include "nanomsg/pair.h"
#include "nanomsg/nn.h"
#include "nanomsg/pair.h"
#include "nanomsg/tcp.h"

#define BUF_LEN 100

int nng_create_socket_to_listen(const char* url);
int nng_create_socket_to_connect(const char* url);