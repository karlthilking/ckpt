/* log.c */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "xnd/shared_cache.h"
#include "xnd/thread_info.h"
#include "xnd/util/log.h"
#include "xnd/util/path.h"

static int log_fd = -1;
static int log_level = XND_DEFAULT_LOG_LEVEL;
static pid_t log_pid = -1;

void xnd_log_setup(void)
{
	int fd;
	size_t len;
	char *level_str, pid_str[11];

	level_str = getenv("XND_LOG_LEVEL");
	if (level_str != NULL)
		log_level = min(atoi(level_str), XND_MAX_LOG_LEVEL);

	if (log_level < XND_TRACING)
		return;

	log_pid = _real_getpid();
	fd = open("xnd.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0) {
		xnd_perror("open");
		return;
	}

	log_fd = dup2(fd, XND_TRACE_FD);
	close(fd);
	if (log_fd < 0) {
		xnd_perror("dup2");
		return;
	}

	snprintf(pid_str, sizeof(pid_str), "%d", log_pid);
	len = strlen("| Start of log entry (pid: ) |") + strlen(pid_str);

#define SET_LINE()					\
	do {						\
		dprintf(log_fd, "+");			\
		for (int i = 1; i < len - 1; i++)	\
			dprintf(log_fd, "-");		\
		dprintf(log_fd, "+\n");			\
	} while (0)

	SET_LINE();
	dprintf(log_fd, "| Start of log entry (pid: %s) |\n", pid_str);
	SET_LINE();
#undef SET_LINE
}

void xnd_log_cleanup(void)
{
	char pid_str[11];
	size_t len;

	if (log_level < XND_TRACING || log_fd < 0)
		return;

	snprintf(pid_str, sizeof(pid_str), "%d", log_pid);
	len = strlen("|  End of log entry (pid: )  |") + strlen(pid_str);

#define SET_LINE()					\
	do {						\
		dprintf(log_fd, "+");			\
		for (int i = 1; i < len - 1; i++)	\
			dprintf(log_fd, "-");		\
		dprintf(log_fd, "+\n");			\
	} while (0)

	SET_LINE();
	dprintf(log_fd, "|  End of log entry (pid: %s)  |\n", pid_str);
	SET_LINE();
#undef SET_LINE

	close(log_fd);
}

void xnd_log_shared_cache_info(void)
{
	const void *start, *end;
	size_t size;
	char *dyld_env_value;
	const char dyld_env_key[] = "DYLD_SHARED_REGION";

	dyld_env_value = getenv(dyld_env_key);
	start = _dyld_get_shared_cache_range(&size);
	end = (const void *)((uintptr_t)start + size);

	xnd_trace("dyld shared cache info:\n"
		  "        environment: %s=%s\n"
		  " shared cache range: %p-%p %zu\n",
		  dyld_env_key, dyld_env_value, start, end, size);
}

void xnd_log_ckpt_thread_info(struct thread_info *ckpt_thread)
{
	uintptr_t tls, thread_self;
	mach_port_t port;
	const uint port_slot = __TSD_MACH_THREAD_SELF;

	thread_self = (uintptr_t)ckpt_thread->self;
	tls = thread_self + PTHREAD_TLS_OFFSET;
	port = (mach_port_t)(uintptr_t)((void **)tls)[port_slot];

	xnd_trace("checkpoint thread info:\n"
		  "     pthread_self(): 0x%016lx\n"
		  "        tpidrro_el0: 0x%016lx (tsd base)\n"
		  " mach_thread_self(): %u\n",
		  thread_self, tls, (u32)port);
}

void xnd_log_main_thread_info(void)
{
	uintptr_t tls, thread_self, sig, munge;

	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
	thread_self = get_tls_slot(__TSD_THREAD_SELF);
	sig = *(long *)thread_self;
	munge = get_tls_slot(__TSD_PTR_MUNGE);

	xnd_trace("main thread info:\n"
		  "      tpidrro_el0: 0x%016lx (tsd base)\n"
		  "   pthread_self(): 0x%016lx\n"
		  " thread signature: 0x%016lx\n"
		  "      munge token: 0x%016lx\n"
		  "      sig ^ munge: 0x%016lx\n",
		  tls, thread_self, sig, munge, sig ^ munge);
}

void xnd_log_mach_port_info(void)
{
	extern mach_port_t mach_task_self_;
	extern mach_port_t thread_self_trap(void);
	extern mach_port_t task_self_trap(void);
	extern mach_port_t host_self_trap(void);

        xnd_trace("mach port info:\n"
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
