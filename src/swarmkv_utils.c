#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <pthread.h>
#include <asm/errno.h>

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


