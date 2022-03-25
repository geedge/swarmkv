#include "mpack.h"
#include "sds.h"
#include "swarmkv_utils.h"
#include "swarmkv_net.h"
#include "swarmkv_message.h"
#include <string.h>

struct swarmkv_msg
{
	enum swarmkv_msg_type type;
	uuid_t requester_uuid;
	unsigned long long rr_seq;
	int do_not_free;	//local use, do not send.
	union
	{
		struct swarmkv_cmd *cmd;
		struct swarmkv_reply *reply;
	};
};

struct swarmkv_msg* swarmkv_msg_new(uuid_t requester_uuid, unsigned long long txn_sequence)
{	
	struct swarmkv_msg *msg=ALLOC(struct swarmkv_msg, 1);
	msg->rr_seq=txn_sequence;
	uuid_copy(msg->requester_uuid, requester_uuid);
	return msg;
}
void swarmkv_msg_add0_cmd(struct swarmkv_msg* msg, const struct swarmkv_cmd* cmd)
{
	msg->do_not_free=1;
	msg->type=SWARMKV_MSG_TYPE_CMD;
	msg->cmd=(struct swarmkv_cmd*) cmd;
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
		if(msg->cmd && !(msg->do_not_free)) swarmkv_cmd_free(msg->cmd);
		msg->cmd=NULL;
	}
	else
	{
		if(msg->reply) swarmkv_reply_free(msg->reply);
		msg->reply=NULL;
	}
	uuid_clear(msg->requester_uuid);
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

unsigned long long swarmkv_msg_read_rr_sequence(struct swarmkv_msg* msg)
{
	return msg->rr_seq;
}
void swarmkv_msg_read_rr_requester_uuid(struct swarmkv_msg* msg, uuid_t out)
{
	uuid_copy(out, msg->requester_uuid);
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
	msg->rr_seq=mpack_node_u64(mpack_node_map_cstr(root, "rr_seq"));
	tmp_node=mpack_node_map_cstr(root, "requester");
	mpack_node_copy_data(tmp_node, (char*)msg->requester_uuid, sizeof(msg->requester_uuid));

	if(msg->type==SWARMKV_MSG_TYPE_CMD)
	{		
		cmd_node=mpack_node_map_cstr(root, "cmd");
		item=mpack_node_map_cstr(cmd_node, "op");
		args_node=mpack_node_map_cstr(cmd_node, "args");
		enum swarmkv_operator op=swarmkv_cmd_str2op(mpack_node_str(item), mpack_node_strlen(item));
		msg->cmd=swarmkv_cmd_new(op, mpack_node_array_length(args_node));
		for(i=0; i<msg->cmd->n_args; i++)
		{
			item=mpack_node_array_at(args_node, i);
			p=mpack_node_str(item);
			sz=mpack_node_strlen(item);
			msg->cmd->args[i]=sdsnewlen(p, sz);
		}	
		msg->cmd->do_not_reply=mpack_node_int(mpack_node_map_cstr(cmd_node, "dnr"));
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
	char *reply_mpack_buff=NULL;
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
	free(reply_mpack_buff);
	return output;
}
sds swarmkv_msg_serialize(const struct swarmkv_msg* msg)
{
	char *root_mpack_buff=NULL;
	size_t root_mpack_sz=0;

	mpack_writer_t root;	
	size_t i=0;
	

	mpack_writer_init_growable(&root, &root_mpack_buff, &root_mpack_sz);
	mpack_build_map(&root);
	mpack_write_cstr(&root, "msg_type");
	mpack_write_int(&root, msg->type);
	mpack_write_cstr(&root, "rr_seq");
	mpack_write_u64(&root, msg->rr_seq);
	mpack_write_cstr(&root, "requester");
	mpack_write_str(&root, (const char*)&(msg->requester_uuid), sizeof(msg->requester_uuid));
	
	if(msg->type==SWARMKV_MSG_TYPE_CMD)
	{
		mpack_write_cstr(&root, "cmd");
		mpack_build_map(&root);
		mpack_write_cstr(&root, "op");
		mpack_write_cstr(&root, swarmkv_cmd_op2str(msg->cmd->op));
		mpack_write_cstr(&root, "args");
		mpack_build_array(&root);
		for(i=0; i<msg->cmd->n_args; i++)
		{
			mpack_write_str(&root, msg->cmd->args[i], (uint32_t)sdslen(msg->cmd->args[i]));
		}
		mpack_complete_array(&root);
		mpack_write_cstr(&root, "dnr");//do not reply
		mpack_write_int(&root, msg->cmd->do_not_reply);		
		mpack_complete_map(&root);
	}
	else
	{		
		sds reply_mpack_buff=NULL;
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

