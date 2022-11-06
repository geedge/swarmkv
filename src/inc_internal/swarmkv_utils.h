#pragma once

#include <assert.h>

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
const char*	module_name_str(const char*name);
const char* swarmkv_util_pthread_cond_timedwait_error_to_string(const int timed_wait_rv);
char* toLower(char* s);
char* toUpper(char* s);
int stringmatchlen(const char *pattern, int patternLen, const char *string, int stringLen, int nocase);
long long ustime(void);

