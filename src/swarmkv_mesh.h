#include "swarmkv_message.h"
#include "swarmkv_common.h"

struct swarmkv_mesh;
//The swarmkv_mesh_send takes the ownership of msg.
int swarmkv_mesh_send(struct swarmkv_mesh *mesh, int current_thread_id, int dest_thread_id, struct swarmkv_msg *msg);
void swarmkv_mesh_set_on_msg_cb(struct swarmkv_mesh *mesh, on_msg_callback_t *cb_func, void *cb_arg);
struct swarmkv_mesh *swarmkv_mesh_new(struct event_base *evbase[], int nthreads, struct log_handle *logger);
void swarmkv_mesh_free(struct swarmkv_mesh *mesh);
struct mesh_info
{
	long long queued_msgs;
	long long drop_msgs;
};
void swarmkv_mesh_info(struct swarmkv_mesh *mesh, struct mesh_info *info);