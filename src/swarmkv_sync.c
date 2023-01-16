#include "swarmkv_common.h"
#include "swarmkv_utils.h"

#include "sds.h"
#include "uthash.h"
#include "utarray.h"

struct sync_data
{
	int ref_cnt;
	sds key;
	char *blob;
	size_t blob_sz;
};
struct sync_data *sync_data_new(const sds key, char *blob, size_t blob_sz)
{
	struct sync_data *data=ALLOC(struct sync_data, 1);
	data->key=sdsdup(key);
	data->blob=blob;
	data->blob_sz=blob_sz;
	data->ref_cnt=1;
	return data;
}
void sync_data_ref_inc(struct sync_data *data)
{
	data->ref_cnt++;
	return;
}
void sync_data_free(struct sync_data *data)
{
	data->ref_cnt--;
	if(data->ref_cnt==0)
	{
		sdsfree(data->key);
		free(data->blob);
		data->blob_sz=0;
		free(data);
	}
	return;
}
UT_icd ut_sync_data_pointer_icd = {sizeof(struct sync_data **), NULL, NULL, NULL};
struct sync_task
{
	node_t peer;
	UT_array *sync_data_list;
	UT_hash_handle hh;
};
struct sync_master
{
	struct sync_task *task_table;
	long long to_sync;
	long long synced;
};
struct sync_master *sync_master_new(void)
{
	struct sync_master *master=ALLOC(struct sync_master, 1);
	return master;
}
//add_obj take the ownership of the blob
void sync_master_add_obj(struct sync_master *master, const sds key, char *blob, size_t blob_sz, const node_t *peers, size_t n_peer)
{
	struct sync_task *task=NULL;
	struct sync_data *data=NULL;
	data=sync_data_new(key, blob, blob_sz);
	for(size_t i=0; i<n_peer; i++)
	{
		HASH_FIND(hh, master->task_table, peers+i, sizeof(node_t), task);
		if(!task)
		{
			task=ALLOC(struct sync_task, 1);
			node_copy(&task->peer, peers+i);
			utarray_new(task->sync_data_list, &ut_sync_data_pointer_icd);
			HASH_ADD_KEYPTR(hh, master->task_table, &task->peer, sizeof(node_t), task);
		}
		sync_data_ref_inc(data);
		utarray_push_back(task->sync_data_list, &data);
	}
	//dereference data
	sync_data_free(data);
	master->to_sync+=n_peer;
	return;
}

int sync_master_get_cmd(struct sync_master *master, node_t *peer, struct swarmkv_cmd **cmd)
{
	struct sync_task *task=NULL, *tmp=NULL;
	struct sync_data **p=NULL, *data=NULL;
	struct swarmkv_cmd *c=NULL;
	size_t n_data=0;
	*cmd=NULL;
	HASH_ITER(hh, master->task_table, task, tmp)
	{
		n_data=utarray_len(task->sync_data_list);
		c=swarmkv_cmd_new(2+n_data*2);
		c->argv[0]=sdsnew("crdt");
		c->argv[1]=sdsnew("push");
		for(size_t i=0; i<n_data; i++)
		{
			p=utarray_eltptr(task->sync_data_list, i);
			data=*p;
			c->argv[2+i*2]=sdsdup(data->key);
			c->argv[2+i*2+1]=sdsnewlen(data->blob, data->blob_sz);
			sync_data_free(data);
			master->synced++;
		}
		node_copy(peer, &task->peer);
		HASH_DEL(master->task_table, task);
		utarray_free(task->sync_data_list);
		free(task);
		break;
	}
	*cmd=c;
	return (c==NULL?0:1);
}
void sync_master_free(struct sync_master *master)
{
	size_t __attribute__((__unused__))task_cnt=HASH_COUNT(master->task_table);
	assert(task_cnt==0);
	assert(master->to_sync==master->synced);
	free(master);
	return;
}