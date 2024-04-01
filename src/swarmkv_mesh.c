#include "swarmkv_mesh.h"
#include "swarmkv_utils.h"
#include "ringbuf.h"
#include "swarmkv_message.h"
#include "log.h"
#include "swarmkv/swarmkv.h"
#include <sys/eventfd.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>             /* Definition of uint64_t */
#include <assert.h>
#include <string.h>
#include <pthread.h>	//sanity check

#define	MODULE_SWAMRKV_MESH	module_name_str("swarmkv.mesh")
#define	RINGBUF_SIZE		8*16*1024

struct swarmkv_mesh_thread
{
	int thread_id;
	int efd;
	struct event * ev;
	ringbuf_t *ring;
	char *buff;
	ringbuf_worker_t **workers;
	struct swarmkv_mesh *ref_mesh;
	long long enqueue_msgs, dequeue_msgs;
	struct event_base *evbase;
};
struct swarmkv_mesh
{
	size_t nr_thread;
	struct swarmkv_mesh_thread *threads;
	struct log_handle *ref_logger;
	on_msg_callback_t *on_msg_cb_func;
	void *on_msg_cb_arg;
	long long drop_msgs;
	long long n_void_consume;
};
int swarmkv_mesh_send(struct swarmkv_mesh *mesh, int current_thread_id, int dest_thread_id, struct swarmkv_msg *msg)
{
	assert(current_thread_id<mesh->nr_thread);
	assert(current_thread_id != dest_thread_id);
	struct swarmkv_mesh_thread *curr_thr=mesh->threads+current_thread_id;
	struct swarmkv_mesh_thread *dest_thr=mesh->threads+dest_thread_id;
	int tid=swarmkv_gettid((struct swarmkv *)(mesh->on_msg_cb_arg));
	assert(tid==current_thread_id);
	ringbuf_t *dest_ring=dest_thr->ring;
	assert(msg->magic == SWARMKV_MSG_MAGIC);
	ssize_t offset=0;
	int retry=0;
	do{
		offset=ringbuf_acquire(dest_ring, curr_thr->workers[dest_thread_id], sizeof(struct swarmkv_msg*));
		retry++;
	}while(offset==-1 && retry<3);
	if(offset == -1)
	{
		mesh->drop_msgs++;
		log_warn(mesh->ref_logger, MODULE_SWAMRKV_MESH, "ringbuf of thread %d is full", dest_thread_id);
		goto error_out;
	}
	memcpy(dest_thr->buff+offset, &msg, sizeof(struct swarmkv_msg*));
	ringbuf_produce(dest_ring, curr_thr->workers[dest_thread_id]);
	uint64_t val=1;
	ssize_t ret=0;
	ret=write(dest_thr->efd, &val, sizeof(val));
	if(ret != sizeof(uint64_t))
	{
		assert(0);
	}
	curr_thr->enqueue_msgs++;
	return 0;
error_out:
	swarmkv_msg_free(msg);
	return -1;
}
void swarmkv_mesh_set_on_msg_cb(struct swarmkv_mesh *mesh, on_msg_callback_t cb_func, void *cb_arg)
{
	mesh->on_msg_cb_func=cb_func;
	mesh->on_msg_cb_arg=cb_arg;
	return;
}
static void mesh_on_eventfd_read_cb(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_mesh_thread *thr=(struct swarmkv_mesh_thread*)arg;
	struct swarmkv_mesh *mesh=thr->ref_mesh;
	ringbuf_t *ring=thr->ring;
	uint64_t n_msg=0;

	int tid=swarmkv_gettid((struct swarmkv *)(mesh->on_msg_cb_arg));
	assert(tid==thr->thread_id);

	ssize_t s = read(thr->efd, &n_msg, sizeof(uint64_t));
	if(s!=sizeof(uint64_t))
	{
		assert(0);
	}
	size_t offset=0, len=0;
	
	struct swarmkv_msg *msg=NULL;
	int i=0;
	while(i<n_msg)
	{
		len=ringbuf_consume(ring, &offset);
		if(len==0)
		{
			mesh->n_void_consume++;
			continue;
		}
		//The ringbuf adopts a two-phase write, two concurrent producers may generates gap in the buffer.
		//So it is possible that consumer wakes up, but sees a zero-length message.
		msg=*(struct swarmkv_msg**)(thr->buff+offset);
		assert(msg->magic==SWARMKV_MSG_MAGIC);
		ringbuf_release(ring, sizeof(struct swarmkv_msg*));
		mesh->on_msg_cb_func(msg, mesh->on_msg_cb_arg);
		thr->dequeue_msgs++;
		offset=0;
		i++;
	}
	return;
}
struct swarmkv_mesh *swarmkv_mesh_new(struct event_base *evbase[], int nthreads, struct log_handle *logger)
{
	struct swarmkv_mesh *mesh=ALLOC(struct swarmkv_mesh, 1);
	mesh->nr_thread=nthreads;
	mesh->ref_logger=logger;
	mesh->threads=ALLOC(struct swarmkv_mesh_thread, nthreads);
	size_t ringbuf_obj_size=0;
	ringbuf_get_sizes(nthreads, &ringbuf_obj_size, NULL);
	for(int i=0; i<mesh->nr_thread; i++)
	{
		mesh->threads[i].thread_id=i;
		mesh->threads[i].efd=eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC );//EFD_NONBLOCK|EFD_CLOEXEC EFD_SEMAPHORE
		mesh->threads[i].workers=ALLOC(ringbuf_worker_t*, nthreads);
		mesh->threads[i].buff=ALLOC(char, RINGBUF_SIZE);
		mesh->threads[i].ring=malloc(ringbuf_obj_size);
		ringbuf_setup(mesh->threads[i].ring, nthreads, RINGBUF_SIZE);
		if(mesh->threads[i].efd<0)
		{
			log_fatal(mesh->ref_logger, MODULE_SWAMRKV_MESH, "eventfd() failed: %s", strerror(errno));
			assert(0);
		}
		mesh->threads[i].ev=event_new(evbase[i], mesh->threads[i].efd, EV_READ|EV_PERSIST, mesh_on_eventfd_read_cb, mesh->threads+i);
		//event_priority_set(mesh->threads[i].ev, 1);
		event_add(mesh->threads[i].ev, NULL);
		mesh->threads[i].ref_mesh=mesh;
		mesh->threads[i].evbase=evbase[i];
	}
	for(int i=0; i<mesh->nr_thread; i++)
	{
		for(int j=0; j<mesh->nr_thread; j++)
		{
			if(i==j) continue;
			mesh->threads[i].workers[j]=ringbuf_register(mesh->threads[j].ring, i);
		}
	}
	return mesh;
}
void swarmkv_mesh_free(struct swarmkv_mesh *mesh)
{
	for(int i=0; i<mesh->nr_thread; i++)
	{
		close(mesh->threads[i].efd);
		ringbuf_release(mesh->threads[i].ring, RINGBUF_SIZE);
		free(mesh->threads[i].ring);
		free(mesh->threads[i].workers);
		free(mesh->threads[i].buff);
		event_del(mesh->threads[i].ev);
		event_free(mesh->threads[i].ev);
	}
	free(mesh->threads);
	free(mesh);
}

void swarmkv_mesh_info(struct swarmkv_mesh *mesh, struct mesh_info *info)
{
	//info->queued_msgs=mesh->enqueue_msgs-mesh->dequeue_msgs;
	memset(info, 0, sizeof(struct mesh_info));
	for(int i=0; i<mesh->nr_thread; i++)
	{
		info->queued_msgs+=mesh->threads[i].enqueue_msgs;
		info->queued_msgs-=mesh->threads[i].dequeue_msgs;
	}
	info->enqueue_drops=mesh->drop_msgs;
	return;
}