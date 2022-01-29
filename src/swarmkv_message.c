

#include "mpack.h"
#include "sds.h"
#define MAX_CMD_ARGS	4

struct swarmkv_data_msg
{
	char* node_uuid;
	enum swarmkv_command cmd;
	size_t n_args;
	sds* args[MAX_CMD_ARGS];
};
void add_swarmkv_msg_task()
{
	bufferevent_socket_new(__ev_base, fd, BEV_OPT_DEFER_CALLBACKS | BEV_OPT_THREADSAFE );

}
void send_cmd_to_key_owner()
{

}

void swarmkv_msg_build_set_string(const char* key, size_t keylen, const char* value,	size_t vallen, char** msg_buff, size_t* msglen)
{
	return 0;
}


