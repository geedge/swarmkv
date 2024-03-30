#pragma once

#include <stdlib.h>	//calloc
#include <sys/time.h>//gettimeofday
#include <stdint.h>	//uint64_t

#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#define	FREE(p)	{free(*p);*p=NULL;}

#define	TRUE	1
#define	FALSE	0

#ifndef MAX
#define MAX(a, b)  	(((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b)  	(((a) < (b)) ? (a) : (b))
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			 \
	const typeof( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif
#define timeval_delta_ms(start, end) (((end).tv_sec-(start).tv_sec)*1000 + ((end).tv_usec-(start).tv_usec)/1000)
#define timeval_delta_us(start, end) (((end).tv_sec-(start).tv_sec)*1000*1000 + ((end).tv_usec-(start).tv_usec))
#define	timeval_to_ms(t)	((t).tv_sec*1000+(t).tv_usec/1000)
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)



// Double hashing for fast hashing, 
// reference: Kirsch, Adam, and Michael Mitzenmacher. "Less hashing, same performance: Building a better bloom filter." 
// Algorithms–ESA 2006: 14th Annual European Symposium, Zurich, Switzerland, September 11-13, 2006. Proceedings 14. Springer Berlin Heidelberg, 2006.
// https://www.eecs.harvard.edu/~michaelm/postscripts/rsa2008.pdf 
struct double_hash
{
    uint64_t a;
    uint64_t b;
};
void double_hash_init(struct double_hash *rv, const void *buffer, int len);
int double_hash_generate(const struct double_hash *rv, int i, int m);