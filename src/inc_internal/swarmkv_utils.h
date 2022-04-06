#pragma once
#include <assert.h>
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
const char*	module_name_str(const char*name);
const char* swarmkv_util_pthread_cond_timedwait_error_to_string(const int timed_wait_rv);
char* toLower(char* s);
char* toUpper(char* s);

#ifndef offsetof
/** Return the offset of a field in a structure. */
#define offsetof(TYPE, MEMBER)  __builtin_offsetof (TYPE, MEMBER)
#endif

/**
 * Return pointer to the wrapping struct instance.
 *
 * Example:
 *
 *  struct wrapper {
 *      ...
 *      struct child c;
 *      ...
 *  };
 *
 *  struct child *x = obtain(...);
 *  struct wrapper *w = container_of(x, struct wrapper, c);
 */
#ifndef container_of
#define container_of(ptr, type, member)	__extension__ ({											\
			const typeof(((type *)0)->member) *_ptr = (ptr); 										\
			__attribute__((unused)) type *_target_ptr =	(type *)(ptr);								\
			(type *)(((uintptr_t)_ptr) - offsetof(type, member)); 									\
		})
#endif

