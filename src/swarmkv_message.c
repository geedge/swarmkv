#include "mpack.h"
#include "swarmkv_utils.h"
#include "swarmkv_message.h"
#include <string.h>

struct swarmkv_reply* deserialize_reply(mpack_node_t reply_node)
{
	struct swarmkv_reply* reply=NULL;
	mpack_node_t item, array_node, vtype_item;
	item=mpack_node_map_cstr(reply_node, "t");
	size_t i=0;
	enum swarmkv_reply_type type=mpack_node_int(item);
	switch(type)
	{
		case SWARMKV_REPLY_INTEGER:
			item=mpack_node_map_cstr(reply_node, "int");
			reply=swarmkv_reply_new_integer(mpack_node_i64(item));
			break;
		case SWARMKV_REPLY_DOUBLE:
			item=mpack_node_map_cstr(reply_node, "dval");
			reply=swarmkv_reply_new_double(mpack_node_double(item));
			break;
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_NODE:
			item=mpack_node_map_cstr(reply_node, "str");
			reply=swarmkv_reply_new_string(mpack_node_str(item), mpack_node_strlen(item));
			reply->type=type;//dirty here
			break;
		case SWARMKV_REPLY_VERBATIM:
			item=mpack_node_map_cstr(reply_node, "str");
			vtype_item=mpack_node_map_cstr(reply_node, "vtype");
			reply=swarmkv_reply_new_verbatim(mpack_node_str(item), mpack_node_strlen(item), mpack_node_str(vtype_item));
			break;
		case SWARMKV_REPLY_NIL:
			reply=swarmkv_reply_new_nil();
			break;
		case SWARMKV_REPLY_ARRAY:		
			array_node=mpack_node_map_cstr(reply_node, "arr");
			reply=swarmkv_reply_new_array(mpack_node_array_length(array_node));
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
struct swarmkv_reply *swarmkv_deserialize_reply(const char *blob, size_t size)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, size);
	mpack_tree_parse(&tree);
	
	mpack_node_t root=mpack_tree_root(&tree);
	struct swarmkv_reply *reply=NULL;
	reply=deserialize_reply(root);

	if (mpack_tree_destroy(&tree) != mpack_ok) {
		fprintf(stderr, "An error occurred decoding a reply blob!\n");
		assert(0);
		swarmkv_reply_free(reply);
		return NULL;
	}	
	return reply;
}
struct swarmkv_cmd *swarmkv_msg_deserialize_cmd(const char *blob, size_t size)
{
	mpack_tree_t tree;
	mpack_tree_init_data(&tree, blob, size);
	mpack_tree_parse(&tree);
	
	mpack_node_t item;
	mpack_node_t root=mpack_tree_root(&tree);
	struct swarmkv_cmd *cmd=NULL;
	const char *arg=NULL;
	size_t arg_sz=0;

	cmd=swarmkv_cmd_new(mpack_node_array_length(root));
	for(size_t i=0; i<cmd->argc; i++)
	{
		item=mpack_node_array_at(root, i);
		arg=mpack_node_str(item);
		arg_sz=mpack_node_strlen(item);
		cmd->argv[i]=sdsnewlen(arg, arg_sz);
	}
	if (mpack_tree_destroy(&tree) != mpack_ok) {
		fprintf(stderr, "An error occurred decoding a cmd blob!\n");
		assert(0);
		swarmkv_cmd_free(cmd);
		return NULL;
	}
	return cmd;
}


void swarmkv_msg_serialize_reply(const struct swarmkv_reply* reply, char **blob, size_t *blob_sz)
{
	char *root_mpack_buff=NULL, *element_mpack_buff=NULL;
	size_t root_mpack_sz=0, element_mpack_size, i=0;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &root_mpack_buff, &root_mpack_sz);
	mpack_build_map(&writer);
	mpack_write_cstr(&writer, "t");
	mpack_write_int(&writer, reply->type);
	switch(reply->type)
	{
		case SWARMKV_REPLY_INTEGER:
			mpack_write_cstr(&writer, "int");
			mpack_write_i64(&writer, reply->integer);
			break;
		case SWARMKV_REPLY_DOUBLE:
			mpack_write_cstr(&writer, "dval");
			mpack_write_double(&writer, reply->dval);
			break;
		case SWARMKV_REPLY_STRING:
		case SWARMKV_REPLY_STATUS:
		case SWARMKV_REPLY_ERROR:
		case SWARMKV_REPLY_NODE:
			mpack_write_cstr(&writer, "str");
			mpack_write_str(&writer, reply->str, reply->len);
			break;
		case SWARMKV_REPLY_VERBATIM:
			mpack_write_cstr(&writer, "str");
			mpack_write_str(&writer, reply->str, reply->len);
			mpack_write_cstr(&writer, "vtype");
			mpack_write_cstr(&writer, reply->vtype);			
			break;
		case SWARMKV_REPLY_NIL:
			break;
		case SWARMKV_REPLY_ARRAY:
			mpack_write_cstr(&writer, "arr");
			mpack_build_array(&writer);
			for(i=0; i<reply->n_element; i++)
			{
				swarmkv_msg_serialize_reply(reply->elements[i], &element_mpack_buff, &element_mpack_size);
				mpack_write_object_bytes(&writer, element_mpack_buff, element_mpack_size);
				free(element_mpack_buff);
				element_mpack_size=0;
			}
			mpack_complete_array(&writer);
			break;
		default:
			assert(0);
	}
	mpack_complete_map(&writer);

	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the reply!\n");
		return;
	}
	*blob=root_mpack_buff;
	*blob_sz=root_mpack_sz;

	return;
}
void swarmkv_msg_serialize_cmd(const struct swarmkv_cmd *cmd, char **blob, size_t *blob_sz)
{
	char *root_mpack_buff=NULL;
	size_t root_mpack_sz=0;

	mpack_writer_t writer;	
	size_t i=0;
	mpack_writer_init_growable(&writer, &root_mpack_buff, &root_mpack_sz);
	mpack_build_array(&writer);
	for(i=0; i<cmd->argc; i++)
	{
		mpack_write_str(&writer, cmd->argv[i], (uint32_t)sdslen(cmd->argv[i]));
	}
	mpack_complete_array(&writer);

	if (mpack_writer_destroy(&writer) != mpack_ok) {
		fprintf(stderr, "An error occurred encoding the cmd!\n");
		return;
	}

	*blob=root_mpack_buff;
	*blob_sz=root_mpack_sz;
	return;
}


