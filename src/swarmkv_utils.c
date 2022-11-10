#include "swarmkv_utils.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <pthread.h>
#include <asm/errno.h>
#include <ctype.h> //tolower, toupper
#include <string.h> //memset
#include <stdlib.h>

pid_t gettid()
{
	return syscall(SYS_gettid);
}
const char*	module_name_str(const char*name)
{
	static __thread char module[64];
	snprintf(module,sizeof(module),"%s(%d)", name, gettid());
	return module;
}
const char* swarmkv_util_pthread_cond_timedwait_error_to_string(const int timed_wait_rv)
{
    switch (timed_wait_rv)
    {
        case ETIMEDOUT:
            return "Conditional timed failed, the time specified by abstime to pthread_cond_timedwait() has passed.";
            break;
        case EINVAL:
            return "Conditional timed failed, the value specified by abstime, cond or mutex is invalid.";
            break;
        case EPERM:
            return "Conditional timed failed, the mutex was not owned by the current thread at the time of the call.";
            break;
        default:
            break;
    }
	return "Conditional timed failed, unknown reason.";
}
char* toLower(char* s)
{
  for(char *p=s; *p; p++) *p=tolower(*p);
  return s;
}
char* toUpper(char* s)
{
  for(char *p=s; *p; p++) *p=toupper(*p);
  return s;
}
char* replace_char(char* str, char find, char replace){
    char *current_pos = strchr(str,find);
    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(current_pos,find);
    }
    return str;
}

/* Glob-style pattern matching. */
//source https://github.com/redis/redis/blob/7.0/src/util.c
int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase)
{
    while(patternLen && stringLen) {
        switch(pattern[0]) {
        case '*':
            while (patternLen && pattern[1] == '*') {
                pattern++;
                patternLen--;
            }
            if (patternLen == 1)
                return 1; /* match */
            while(stringLen) {
                if (stringmatchlen(pattern+1, patternLen-1,
                            string, stringLen, nocase))
                    return 1; /* match */
                string++;
                stringLen--;
            }
            return 0; /* no match */
            break;
        case '?':
            string++;
            stringLen--;
            break;
        case '[':
        {
            int not, match;

            pattern++;
            patternLen--;
            not = pattern[0] == '^';
            if (not) {
                pattern++;
                patternLen--;
            }
            match = 0;
            while(1) {
                if (pattern[0] == '\\' && patternLen >= 2) {
                    pattern++;
                    patternLen--;
                    if (pattern[0] == string[0])
                        match = 1;
                } else if (pattern[0] == ']') {
                    break;
                } else if (patternLen == 0) {
                    pattern--;
                    patternLen++;
                    break;
                } else if (patternLen >= 3 && pattern[1] == '-') {
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if (start > end) {
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if (nocase) {
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2;
                    patternLen -= 2;
                    if (c >= start && c <= end)
                        match = 1;
                } else {
                    if (!nocase) {
                        if (pattern[0] == string[0])
                            match = 1;
                    } else {
                        if (tolower((int)pattern[0]) == tolower((int)string[0]))
                            match = 1;
                    }
                }
                pattern++;
                patternLen--;
            }
            if (not)
                match = !match;
            if (!match)
                return 0; /* no match */
            string++;
            stringLen--;
            break;
        }
        case '\\':
            if (patternLen >= 2) {
                pattern++;
                patternLen--;
            }
            /* fall through */
        default:
            if (!nocase) {
                if (pattern[0] != string[0])
                    return 0; /* no match */
            } else {
                if (tolower((int)pattern[0]) != tolower((int)string[0]))
                    return 0; /* no match */
            }
            string++;
            stringLen--;
            break;
        }
        pattern++;
        patternLen--;
        if (stringLen == 0) {
            while(*pattern == '*') {
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && stringLen == 0)
        return 1;
    return 0;
}

/* Return the UNIX time in microseconds */
long long ustime(void) {
    struct timespec tv;
    long long ust;
    clock_gettime(CLOCK_REALTIME, &tv);

    ust = ((long long)tv.tv_sec) * 1000000;
    ust += tv.tv_nsec/1000;
    return ust;
}
int is_number(const char *string, long long *number)
{
	char *endptr=NULL;
	*number=strtol(string, &endptr, 10);
	if(*endptr=='\0')
	{
		return 1;		
	}
    else
    {
        return 0;
    }
}
int is_double(const char *string, double *number)
{
    char *endptr=NULL;
    *number = strtod(string, &endptr);
	if(*endptr=='\0')
	{
		return 1;		
	}
    else
    {
        return 0;
    }
}