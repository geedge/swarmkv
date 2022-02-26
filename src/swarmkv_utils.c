#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

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

