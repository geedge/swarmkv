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

#define	MODULE_SWAMRKV_MESH	module_name_str("swarmkv.mesh")
#define	RINGBUF_SIZE		1024

struct swarmkv_mesh_thread
{
	int thread_id;
	int efd;
	struct event * ev;
	ringbuf_t *ring;
	char *buff;
	ringbuf_worker_t **workers;
	struct swarmkv_mesh *ref_mesh;
};
struct swarmkv_mesh
{
	size_t nr_thread;
	struct swarmkv_mesh_thread *threads;
	struct log_handle *ref_logger;
	on_msg_callback_t *on_msg_recv;
	void *msg_recv_arg;
};
//The swarmkv_mesh_send takes the ownership of msg.
int swarmkv_mesh_send(struct swarmkv_mesh *mesh, int current_thread_id, int dest_thread_id, struct swarmkv_msg *msg)
{
	assert(current_thread_id<mesh->nr_thread);
	struct swarmkv_mesh_thread *curr_thr=mesh->threads+current_thread_id;
	struct swarmkv_mesh_thread *dest_thr=mesh->threads+dest_thread_id;
	ringbuf_t *dest_ring=dest_thr->ring;
	if(curr_thr->workers[dest_thread_id]==NULL)
	{
		curr_thr->workers[dest_thread_id]=ringbuf_register(dest_ring, current_thread_id);
		assert(curr_thr->workers[dest_thread_id]);
	}
	ssize_t offset=0;
	offset=ringbuf_acquire(dest_ring, curr_thr->workers[dest_thread_id], sizeof(struct swarmkv_msg*));
	if(offset == -1)
	{
		log_warn(mesh->ref_logger, MODULE_SWAMRKV_MESH, "ringbuf is full, drop the message");
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
	return 0;
error_out:
	swarmkv_msg_free(msg);
	return -1;
}
void swarmkv_mesh_set_on_msg_cb(struct swarmkv_mesh *mesh, on_msg_callback_t cb_func, void *cb_arg)
{
	mesh->on_msg_recv=cb_func;
	mesh->msg_recv_arg=cb_arg;
	return;
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
static void swarmkv_mesh_on_event(evutil_socket_t fd, short what, void * arg)
{
	struct swarmkv_mesh_thread *thread=(struct swarmkv_mesh_thread*)arg;
	struct swarmkv_mesh *mesh=thread->ref_mesh;
	ringbuf_t *ring=thread->ring;
	uint64_t val=0;
	ssize_t s = read(thread->efd, &val, sizeof(uint64_t));
	if(s!=sizeof(uint64_t))
	{
		assert(0);
	}
	size_t offset=0, ret=0, n_msg=0;
	struct swarmkv_msg *msg=NULL;
	while(1)
	{
		ret=ringbuf_consume(ring, &offset);
		if(ret==0)
		{
			break;
		}
		ringbuf_release(ring, sizeof(struct swarmkv_msg*));
		msg=*(struct swarmkv_msg**)(thread->buff+offset);
		n_msg++;
		mesh->on_msg_recv(msg, mesh->msg_recv_arg);
		swarmkv_msg_free(msg);
	}
	assert(n_msg==val);
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
		mesh->threads[i].efd=eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
		mesh->threads[i].workers=ALLOC(ringbuf_worker_t*, nthreads);
		mesh->threads[i].buff=ALLOC(char, RINGBUF_SIZE);
		mesh->threads[i].ring=malloc(ringbuf_obj_size);
		ringbuf_setup(mesh->threads[i].ring, nthreads, RINGBUF_SIZE);
		if(mesh->threads[i].efd<0)
		{
			log_fatal(mesh->ref_logger, MODULE_SWAMRKV_MESH, "eventfd() failed: %s", strerror(errno));
			assert(0);
		}
		mesh->threads[i].ev=event_new(evbase[i], mesh->threads[i].efd, EV_READ|EV_PERSIST, swarmkv_mesh_on_event, mesh->threads+i);
		event_add(mesh->threads[i].ev, NULL);
		mesh->threads[i].ref_mesh=mesh;
	}
	return mesh;
}