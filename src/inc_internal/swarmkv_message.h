#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

enum MSG_TYPE
{
	MSG_TYPE_CMD,
	MSG_TYPE_REPLY
};
#define	SWARMKV_MSG_MAGIC	0x5210
struct swarmkv_msg
{
    unsigned int magic;
    enum MSG_TYPE type;
    long long sequence;
    int caller_tid;
    unsigned int payload_len;
    node_t caller;
    node_t executor;
    union
    {
        struct swarmkv_cmd *cmd;
        struct swarmkv_reply *reply;
        void *payload;
    };
};
#define SWARMKV_MSG_HDR_SIZE    offsetof(struct swarmkv_msg, payload)

struct swarmkv_msg *swarmkv_msg_new_by_cmd(const struct swarmkv_cmd *cmd, const node_t *caller, int caller_tid, const node_t *executor, long long sequence);
struct swarmkv_msg *swarmkv_msg_new_by_reply(const struct swarmkv_reply *reply, const node_t *caller, int caller_tid, const node_t *executor, long long sequence);
void swarmkv_msg_free(struct swarmkv_msg *msg);

//on_msg_callback_t transfers ownership of msg to the callback function
typedef void on_msg_callback_t(struct swarmkv_msg *msg, void *arg);


void swarmkv_msg_serialize(const struct swarmkv_msg *msg, char **blob, size_t *blob_sz);
struct swarmkv_msg *swarmkv_msg_deserialize(const char *blob, size_t size);