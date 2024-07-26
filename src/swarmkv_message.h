#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

#include <stdint.h>

enum swarmkv_msg_type
{
    SWARMKV_MSG_TYPE_CMD,
    SWARMKV_MSG_TYPE_REPLY
};
struct swarmkv_msg
{
    enum swarmkv_msg_type type;
    uint64_t sequence;
    union
    {
        struct swarmkv_cmd *cmd;
        struct swarmkv_reply *reply;
    };
};

struct swarmkv_msg *swarmkv_msg_new_by_cmd(const struct swarmkv_cmd *cmd, long long sequence);
struct swarmkv_msg *swarmkv_msg_new_by_reply(const struct swarmkv_reply *reply, long long sequence);
void swarmkv_msg_free(struct swarmkv_msg *msg);

void swarmkv_msg_serialize(const struct swarmkv_msg *msg, char **blob, size_t *blob_sz);
struct swarmkv_msg *swarmkv_msg_deserialize(const char *blob, size_t size);