#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"

void swarmkv_msg_serialize_reply(const struct swarmkv_reply* reply, char **blob, size_t *blob_sz);
struct swarmkv_reply *swarmkv_deserialize_reply(const char *blob, size_t size);

void swarmkv_msg_serialize_cmd(const struct swarmkv_cmd *cmd, char **blob, size_t *blob_sz);
struct swarmkv_cmd *swarmkv_msg_deserialize_cmd(const char *blob, size_t size);

