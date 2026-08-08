/* log.c */
#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "xnd/shared_cache.h"
#include "xnd/util/log.h"
#include "xnd/util/path.h"

#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

static int log_fd = -1;
static int log_level = XND_DEFAULT_LOG_LEVEL;

void xnd_log_setup(void)
{
	int fd;
	char *level_str;

	level_str = getenv("XND_LOG_LEVEL");
	if (level_str != NULL) {
		log_level = min(atoi(level_str), XND_MAX_LOG_LEVEL);
	}

	if (log_level < XND_TRACING) {
		return;
	}

	fd = open("xnd.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd != -1) {
		log_fd = dup2(fd, XND_TRACE_FD);
		close(fd);
	}

	if (log_fd != -1) {
		dprintf(log_fd,
			"+------------------------------+\n"
			"| Start of log entry (pid: %d) |\n"
			"+------------------------------+\n",
			_real_getpid());
	}
}

void xnd_log_cleanup(void)
{
	if (log_level >= XND_TRACING && log_fd != -1) {
		dprintf(log_fd,
			"+------------------------------+\n"
			"|  End of log entry (pid: %d)  |\n"
			"+------------------------------+\n",
			_real_getpid());
		close(log_fd);
	}
}

void xnd_log_shared_cache_info(void)
{
        char            *dyld_env;
        const void      *base;
        size_t          size;

        dyld_env = getenv("DYLD_SHARED_REGION");
        base = _dyld_get_shared_cache_range(&size);

        xnd_trace("dyld shared cache info:\n"
                  "      DYLD_SHARED_REGION=%s\n"
                  " dyld shared cache range: %p-%p %zu\n",
                  dyld_env, base, base + size, size);
}

void xnd_log_ckpt_thread_info(struct thread_info *ckpt_thread)
{
        uintptr_t       tls, thread_self;
        mach_port_t     port;

        thread_self = (uintptr_t)ckpt_thread->self;
        tls = thread_self + PTHREAD_T_TLS_OFFSET;
        port = (mach_port_t)(uintptr_t)((void **)tls)[__TSD_MACH_THREAD_SELF];

        xnd_trace("Checkpoint thread info:\n"
                  "        pthread_self(): 0x%lx\n"
                  "           tpidrro_el0: 0x%lx\n"
                  "    mach_thread_self(): %u\n",
                  thread_self, tls, (u32)port);
}

void xnd_log_main_thread_info(void)
{
        uintptr_t tls, main_thread, signature, munge_token;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        main_thread = get_tls_slot(__TSD_THREAD_SELF);
        signature = *(uintptr_t *)main_thread;
        munge_token = get_tls_slot(__TSD_PTR_MUNGE);

        xnd_trace("Main thread info:\n"
                  "            tpiddro_el0: 0x%lx\n"
                  "         pthread_self(): 0x%lx\n"
                  "      pthread signature: 0x%lx\n"
                  "    pthread munge token: 0x%lx\n"
                  "      sig ^ munge_token: 0x%lx\n",
                  tls, main_thread, signature, munge_token,
                  signature ^ munge_token);
}

extern mach_port_t mach_task_self_;
extern mach_port_t thread_self_trap(void);
extern mach_port_t task_self_trap(void);
extern mach_port_t host_self_trap(void);

void xnd_log_mach_port_info(void)
{
        xnd_trace("Mach port info:\n"
                  " mach_task_self(): %u\n"
                  " task_self_trap(): %u\n"
                  " mach_task_self_: %u\n"
                  "\n"
                  " mach_host_self(): %u\n"
                  " host_self_trap(): %u\n"
                  "\n"
                  " mach_thread_self(): %u\n"
                  " thread_self_trap(): %u\n",
                  mach_task_self(), task_self_trap(), mach_task_self_,
                  mach_host_self(), host_self_trap(),
                  mach_thread_self(), thread_self_trap());
}
