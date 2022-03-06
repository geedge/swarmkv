#include "mpack.h"
#include "sds.h"
#include "swarmkv_utils.h"
#include "swarmkv_net.h"
#include "swarmkv_message.h"
#include <string.h>

struct swarmkv_msg
{
	enum swarmkv_msg_type type;
	uuid_t txn_initiator_uuid;
	unsigned long long txn_seq;
	int do_not_reply;//only used in cmd msg
	union
	{
		struct swarmkv_cmd* cmd;
		struct swarmkv_reply* reply;
	};
};

struct swarmkv_msg* swarmkv_msg_new(uuid_t initiator_uuid, unsigned long long txn_sequence)
{	
	struct swarmkv_msg* msg=ALLOC(struct swarmkv_msg, 1);
	msg->txn_seq=txn_sequence;
	uuid_copy(msg->txn_initiator_uuid, initiator_uuid);
	return msg;
}
void swarmkv_msg_add_cmd(struct swarmkv_msg* msg, struct swarmkv_cmd* cmd)
{
	msg->type=SWARMKV_MSG_TYPE_CMD;
	msg->cmd=cmd;
}
void swarmkv_msg_add_reply(struct swarmkv_msg* msg, struct swarmkv_reply* reply)
{
	msg->type=SWARMKV_MSG_TYPE_REPLY;
	msg->reply=reply;
}

void swarmkv_msg_free(struct swarmkv_msg* msg)
{
	if(msg->type==SWARMKV_MSG_TYPE_CMD)
	{
		if(msg->cmd) swarmkv_cmd_free(msg->cmd);
		msg->cmd=NULL;
	}
	else
	{
		if(msg->reply) swarmkv_reply_free(msg->reply);
		msg->reply=NULL;
	}
	uuid_clear(msg->txn_initiator_uuid);
	free(msg);
	return;
}

enum swarmkv_msg_type swarmkv_msg_get_type(struct swarmkv_msg* msg)
{
	return msg->type;
}
struct swarmkv_reply* swarmkv_msg_get_reply(struct swarmkv_msg* msg)
{
	struct swarmkv_reply *r=msg->reply;
	assert(msg->type==SWARMKV_MSG_TYPE_REPLY);
	msg->reply=NULL;
	return r;
}
struct swarmkv_cmd* swarmkv_msg_get_cmd(struct swarmkv_msg* msg)
{
	struct swarmkv_cmd* cmd=msg->cmd;
	assert(msg->type==SWARMKV_MSG_TYPE_CMD);
	msg->cmd=NULL;
	return cmd;
}

unsigned long long swarmkv_msg_read_transaction_sequence(struct swarmkv_msg* msg)
{
	return msg->txn_seq;
}
void swarmkv_msg_read_txn_initiator_uuid(struct swarmkv_msg* msg, uuid_t out)
{
	uuid_copy(out, msg->txn_initiator_uuid);
	return;
}
struct swarmkv_reply* deserialize_reply(mpack_node_t reply_node)
{
	struct swarmkv_reply* reply=ALLOC(struct swarmkv_reply, 1);
	mpack_node_t item, array_node;
	item=mpack_node_map_cstr(reply_node, "reply_type");
	reply->type=mpack_node_int(item);
	size_t i=0;
	switch(reply->type)
	{
		case SWARMKV_REPLY_INTEGER:
			item=mpack_node_map_cstr(reply_node, "integer");
			reply->integer=mpack_node_int(item);
			break;
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_STATUS:
			item=mpack_node_map_cstr(reply_node, "string");
			reply->len=mpack_node_strlen(item);
			reply->str=ALLOC(char, reply->len);
			memcpy(reply->str, mpack_node_str(item), reply->len);
			break;
		case SWARMKV_REPLY_NIL:
			break;
		case SWARMKV_REPLY_ARRAY:
			array_node=mpack_node_map_cstr(reply_node, "element");
			reply->n_element=mpack_node_array_length(array_node);
			reply->elements=ALLOC(struct swarmkv_reply*, reply->n_element);
			for(i=0; i<reply->n_element; i++)
			{
				item=mpack_node_array_at(array_node, i);
				reply->elements[i]=deserialize_reply(item);
			}
			break;
		default:
			assert(0);
			break;
	}
	return reply;
}
struct swarmkv_msg* swarmkv_msg_deserialize(const char* data, size_t len)
{
	struct swarmkv_msg* msg=ALLOC(struct swarmkv_msg, 1);
	size_t i=0;
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, data, len);
	mpack_tree_parse(&tree);
	mpack_node_t tmp_node, item, cmd_node, args_node, reply_node;
	const char *p=NULL;
	size_t sz;
	mpack_node_t root=mpack_tree_root(&tree);
	msg->type=mpack_node_int(mpack_node_map_cstr(root, "msg_type"));
	msg->txn_seq=mpack_node_u64(mpack_node_map_cstr(root, "txn_seq"));
	tmp_node=mpack_node_map_cstr(root, "txn_initiator");
	mpack_node_copy_data(tmp_node, (char*)msg->txn_initiator_uuid, sizeof(msg->txn_initiator_uuid));

	if(msg->type==SWARMKV_MSG_TYPE_CMD)
	{		
		msg->do_not_reply=mpack_node_int(mpack_node_map_cstr(root, "do_not_reply"));
		msg->cmd=ALLOC(struct swarmkv_cmd, 1);
		cmd_node=mpack_node_map_cstr(root, "cmd");
		item=mpack_node_map_cstr(cmd_node, "op");
//		msg->cmd->op=mpack_node_int(item);
		args_node=mpack_node_map_cstr(cmd_node, "args");
//		msg->cmd->n_args=mpack_node_array_length(args_node);
		msg->cmd=swarmkv_cmd_new(mpack_node_int(item), mpack_node_array_length(args_node));
		for(i=0; i<msg->cmd->n_args; i++)
		{
			item=mpack_node_array_at(args_node, i);
			p=mpack_node_str(item);
			sz=mpack_node_strlen(item);
			msg->cmd->args[i]=sdsnewlen(p, sz);
		}
	}
	else
	{
		reply_node=mpack_node_map_cstr(root, "reply");
		msg->reply=deserialize_reply(reply_node);	
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) {
		fprintf(stderr, "An error occurred decoding the data!\n");
		swarmkv_msg_free(msg);
		return NULL;
	}
	return msg;
}

sds serialize_reply(const struct swarmkv_reply* reply)
{
	char *reply_mpack_buff;
	size_t reply_mpack_sz=0, i=0;
	sds element=NULL, output=NULL;;
	mpack_writer_t reply_mpack;
	mpack_writer_init_growable(&reply_mpack, &reply_mpack_buff, &reply_mpack_sz);
	mpack_build_map(&reply_mpack);
	mpack_write_cstr(&reply_mpack, "reply_type");
	mpack_write_int(&reply_mpack, reply->type);
	switch(reply->type)
	{
		case SWARMKV_REPLY_INTEGER:
			mpack_write_cstr(&reply_mpack, "integer");
			mpack_write_int(&reply_mpack, reply->integer);
			break;
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:			
			mpack_write_cstr(&reply_mpack, "string");
			mpack_write_str(&reply_mpack, reply->str, reply->len);
			break;
		case SWARMKV_REPLY_NIL:
			break;
		case SWARMKV_REPLY_ARRAY:
			mpack_write_cstr(&reply_mpack, "element");
			mpack_build_array(&reply_mpack);
			for(i=0; i<reply->n_element; i++)
			{
				element=serialize_reply(reply->elements[i]);
				mpack_write_object_bytes(&reply_mpack, element, sdslen(element));
				sdsfree(element);
			}
			mpack_complete_array(&reply_mpack);
			break;
		default:
			assert(0);
	}
	mpack_complete_map(&reply_mpack);
	mpack_writer_destroy(&reply_mpack);
	output=sdsnewlen(reply_mpack_buff, reply_mpack_sz);
	return output;
}
sds swarmkv_msg_serialize(const struct swarmkv_msg* msg)
{
	char *root_mpack_buff=NULL, *cmd_mpack_buff=NULL, *args_mpack_buff=NULL;
	size_t root_mpack_sz=0, cmd_mpack_sz=0, args_mpack_sz=0;
	sds reply_mpack_buff;

	mpack_writer_t root, cmd, args;	
	size_t i=0;
	

	mpack_writer_init_growable(&root, &root_mpack_buff, &root_mpack_sz);
	mpack_build_map(&root);
	mpack_write_cstr(&root, "msg_type");
	mpack_write_int(&root, msg->type);
	mpack_write_cstr(&root, "txn_seq");
	mpack_write_u64(&root, msg->txn_seq);
	mpack_write_cstr(&root, "txn_initiator");
	mpack_write_str(&root, (const char*)&(msg->txn_initiator_uuid), sizeof(msg->txn_initiator_uuid));
	
	if(msg->type==SWARMKV_MSG_TYPE_CMD)
	{
		mpack_write_cstr(&root, "do_not_reply");
		mpack_write_int(&root, msg->do_not_reply);
		
		mpack_writer_init_growable(&args, &args_mpack_buff, &args_mpack_sz);
		mpack_build_array(&args);
		for(i=0; i<msg->cmd->n_args; i++)
		{
			mpack_write_str(&args, msg->cmd->args[i], (uint32_t)sdslen(msg->cmd->args[i]));
		}
		mpack_complete_array(&args);
		mpack_writer_destroy(&args);
		
		mpack_writer_init_growable(&cmd, &cmd_mpack_buff, &cmd_mpack_sz);
		mpack_build_map(&cmd);
		mpack_write_cstr(&cmd, "op");
		mpack_write_int(&cmd, msg->cmd->op);
		mpack_write_cstr(&cmd, "args");
		mpack_write_object_bytes(&cmd, args_mpack_buff, args_mpack_sz);
		mpack_complete_map(&cmd);
		mpack_writer_destroy(&cmd);
		free(args_mpack_buff);
		args_mpack_buff=NULL;

		mpack_write_cstr(&root, "cmd");
		mpack_write_object_bytes(&root, cmd_mpack_buff, cmd_mpack_sz);
		free(cmd_mpack_buff);
		cmd_mpack_buff=NULL;
	}
	else
	{
		mpack_write_cstr(&root, "reply");
		reply_mpack_buff=serialize_reply(msg->reply);
		mpack_write_object_bytes(&root, reply_mpack_buff, sdslen(reply_mpack_buff));
		sdsfree(reply_mpack_buff);
		reply_mpack_buff=NULL;
	}
	mpack_complete_map(&root);
	mpack_writer_destroy(&root);
	sds output=sdsnewlen(root_mpack_buff, root_mpack_sz);
	free(root_mpack_buff);
	return output;
}

