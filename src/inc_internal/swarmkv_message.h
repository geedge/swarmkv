#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

enum MSG_TYPE
{
	MSG_TYPE_CMD,
	MSG_TYPE_REPLY
};
struct swarmkv_msg
{
    enum MSG_TYPE type;
    long long sequence;
    int thread_id;
    node_t source;
    union
    {
        struct swarmkv_cmd *cmd;
        struct swarmkv_reply *reply;
    };
};
struct swarmkv_msg *swarmkv_msg_new_by_cmd(const struct swarmkv_cmd *cmd, const node_t *source, int thread_id, long long sequence);
void swarmkv_msg_free(struct swarmkv_msg *msg);
typedef void on_msg_callback_t(const struct swarmkv_msg *msg, void *arg);
void swarmkv_msg_serialize_reply(const struct swarmkv_reply *reply, char **blob, size_t *blob_sz);
struct swarmkv_reply *swarmkv_deserialize_reply(const char *blob, size_t size);

void swarmkv_msg_serialize_cmd(const struct swarmkv_cmd *cmd, char **blob, size_t *blob_sz);
struct swarmkv_cmd *swarmkv_msg_deserialize_cmd(const char *blob, size_t size);

