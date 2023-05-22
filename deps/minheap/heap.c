#include "heap.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void min_heap_shift_down_(min_heap_t* s, unsigned hole_index, void* node);
static void min_heap_shift_up_(min_heap_t* s, unsigned hole_index, void *node);
static void min_heap_shift_up_unconditional_(min_heap_t* s, unsigned hole_index, void* node);

struct entry_data {
    char *str;
    size_t len;
};

int init_heap(struct minheap *heap, int max_size, minheap_cmp cmp, minheap_getindex get, minheap_setindex set)
{
	heap->max_size = max_size;
	heap->cur_size = 0;
	heap->nodes = malloc(sizeof(void *) * max_size);
	if (!heap->nodes)
		return (-1);
	assert(cmp);
	heap->cmp = cmp;
	heap->get = get;
	heap->set = set;
	return (0);
}

int get_node_index(struct minheap *heap, void *node)
{
	assert(heap->get);
	int index = heap->get(node);
	assert(heap->cur_size > index);
	assert(heap->nodes[index] == node);
	return index;
}

int push_heap(struct minheap *heap, void *node)
{
	if (heap->cur_size >= heap->max_size)
		return (-1);
	min_heap_shift_up_(heap, heap->cur_size++, node);	
	return (0);
}
void *pop_heap(struct minheap *heap)
{
    if (heap->cur_size <= 0)
    {
        return NULL;
    }

    void *ret = heap->nodes[0];
	min_heap_shift_down_(heap, 0u, heap->nodes[--heap->cur_size]);
	return ret;
}
int adjust_heap_node(struct minheap *heap, void *node)
{
	int index = get_node_index(heap, node);
	unsigned parent = (index - 1) / 2;	

	if (index > 0 && heap->cmp(node, heap->nodes[parent]))
		min_heap_shift_up_unconditional_(heap, index, node);
	else
		min_heap_shift_down_(heap, index, node);
	return 0;
}

int erase_heap_node(struct minheap* heap, void *node)
{
	int index = get_node_index(heap, node);
	void *last = heap->nodes[--heap->cur_size];
	unsigned parent = (index - 1) / 2;
	if (index > 0 && heap->cmp(last, heap->nodes[parent]))
		min_heap_shift_up_unconditional_(heap, index, last);
	else
		min_heap_shift_down_(heap, index, last);
	return (0);
}

static void min_heap_shift_up_unconditional_(min_heap_t* s, unsigned hole_index, void* node)
{
	assert(s->set);
    unsigned parent = (hole_index - 1) / 2;
    do
    {
		s->nodes[hole_index] = s->nodes[parent];
		s->set(hole_index, s->nodes[hole_index]);
		hole_index = parent;
		parent = (hole_index - 1) / 2;
    } while (hole_index && s->cmp(node, s->nodes[parent]));
    s->nodes[hole_index] = node;
	s->set(hole_index, s->nodes[hole_index]);	
}

static void min_heap_shift_up_(min_heap_t* s, unsigned hole_index, void *node)
{
	assert(s->set);	
    unsigned parent = (hole_index - 1) / 2;
    while (hole_index && s->cmp(node, s->nodes[parent]))
	{
		s->nodes[hole_index] = s->nodes[parent];
		s->set(hole_index, s->nodes[hole_index]);				
		hole_index = parent;
		parent = (hole_index - 1) / 2;
	}
	s->nodes[hole_index] = node;
	s->set(hole_index, s->nodes[hole_index]);		
}

static void min_heap_shift_down_(min_heap_t* s, unsigned hole_index, void* node)
{
	assert(s->set);	
    unsigned min_child = 2 * (hole_index + 1);
    while (min_child <= s->cur_size)
	{
		if (min_child == s->cur_size
			|| s->cmp(s->nodes[min_child - 1], s->nodes[min_child]))
			--min_child;
		if (s->cmp(node, s->nodes[min_child]))
			break;
		s->nodes[hole_index] = s->nodes[min_child];
		s->set(hole_index, s->nodes[hole_index]);				
		hole_index = min_child;
		min_child = 2 * (hole_index + 1);
	}
	s->nodes[hole_index] = node;
	s->set(hole_index, s->nodes[hole_index]);			
}
