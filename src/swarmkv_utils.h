#pragma once

#include <assert.h>
#include <stddef.h>

#define ALLOC(type, number) ((type *)calloc(sizeof(type), number))
#define	FREE(p)	{free(*p);*p=NULL;}

#define timespec_diff_usec(start, end)  (((end)->tv_sec-(start)->tv_sec)*1000*1000 + ((end)->tv_nsec-(start)->tv_nsec)/1000)

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

#define atomic_inc(x) __sync_add_and_fetch((x),1)
#define atomic_dec(x) __sync_sub_and_fetch((x),1)
#define atomic_add(x,y) __sync_add_and_fetch((x),(y))
#define atomic_sub(x,y) __sync_sub_and_fetch((x),(y))
#define atomic_read(x) __sync_add_and_fetch((x),0)
#define atomic_set(x,y) __sync_lock_test_and_set((x),y)

const char *module_name_str(const char *name);
const char *swarmkv_util_pthread_cond_timedwait_error_to_string(const int timed_wait_rv);
char *toLower(char* s);
char *toUpper(char* s);
// Glob-style pattern matching.
// Return 1 if matched, 0 if not matched
int stringmatchlen(const char *pattern, int patternLen, const char *string, int stringLen, int nocase);
long long ustime(void);
int is_number(const char *string, size_t sz, long long *number);
int is_double(const char *string, double *number);