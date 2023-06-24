#include "swarmkv_message.h"
#include "swarmkv_common.h"

struct swarmkv_mesh;
int swarmkv_mesh_send(struct swarmkv_mesh *mesh, int current_thread_id, int dest_thread_id, struct swarmkv_msg *msg);
void swarmkv_mesh_set_on_msg_recv_cb(struct swarmkv_mesh *mesh, on_msg_callback_t *cb_func, void *cb_arg);
struct swarmkv_mesh *swarmkv_mesh_new(struct event_base *evbase[], int nthreads, struct log_handle *logger);