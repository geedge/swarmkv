#ifndef MINHEAP_H
#define MINHEAP_H

#include <stdbool.h>

typedef bool (*minheap_cmp)(void *a, void *b);
typedef int (*minheap_getindex)(void *a);
typedef void (*minheap_setindex)(int index, void *a);	

typedef struct minheap
{
	int max_size;
	int cur_size;
	void **nodes;
	minheap_cmp cmp;
	minheap_getindex get;
	minheap_setindex set;
} min_heap_t;

typedef struct heap_entry {
	int index;
    void* key;   // Key for this entry
    void* value; // Value for this entry
} heap_entry;

typedef min_heap_t heap;

int init_heap(struct minheap *heap, int max_size, minheap_cmp cmp, minheap_getindex get, minheap_setindex set);
int get_node_index(struct minheap *heap, void *node);

int push_heap(struct minheap *heap, void *node);
void *pop_heap(struct minheap *heap);
int adjust_heap_node(struct minheap *heap, void *node);
int erase_heap_node(struct minheap* heap, void *node);
#endif /* MINHEAP_H */
