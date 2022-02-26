#pragma once
#include "swarmkv/swarmkv.h"
#include "swarmkv_common.h"
#include "swarmkv_limits.h"

enum swarmkv_msg_type
{
	SWARMKV_MSG_TYPE_INVALID=0,
	SWARMKV_MSG_TYPE_CMD=1,
	SWARMKV_MSG_TYPE_REPLY
};
struct swarmkv_msg;

struct swarmkv_msg* swarmkv_msg_new(uuid_t initiator_uuid, unsigned long long txn_sequence);
//swarmkv_msg_add_** obtains data ownership from the caller.
void swarmkv_msg_add_cmd(struct swarmkv_msg *msg, struct swarmkv_cmd *cmd);
void swarmkv_msg_add_reply(struct swarmkv_msg *msg, struct swarmkv_reply *reply);

void swarmkv_msg_free(struct swarmkv_msg *msg);

enum swarmkv_msg_type swarmkv_msg_get_type(struct swarmkv_msg *msg);

//swarmkv_msg_get_** transfers data ownership to the caller
struct swarmkv_reply* swarmkv_msg_get_reply(struct swarmkv_msg *msg);
struct swarmkv_cmd* swarmkv_msg_get_cmd(struct swarmkv_msg *msg);

unsigned long long swarmkv_msg_read_transaction_sequence(struct swarmkv_msg *msg);
void swarmkv_msg_read_txn_initiator_uuid(struct swarmkv_msg* msg, uuid_t out);

struct swarmkv_msg* swarmkv_msg_deserialize(const char* data, size_t len);
sds swarmkv_msg_serialize(const struct swarmkv_msg *msg);

