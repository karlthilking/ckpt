/* log.c */
#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "xnd/shared_cache.h"
#include "xnd/util/log.h"
#include "xnd/util/path.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <mach-o/dyld.h>

static int log_level = XND_DEFAULT_LOG_LEVEL;
static int level_to_fd[] = { 
        XND_ERROR_FD, XND_WARN_FD, XND_DEBUG_FD, XND_TRACE_FD
};

void xnd_log_setup(void)
{
        char    *level_str;
        int     level;
        
        level_str = getenv("XND_LOG_LEVEL");
        if (level_str) {
                level = min(atoi(level_str), XND_MAX_LOG_LEVEL);
        } else {
                level = XND_DEFAULT_LOG_LEVEL;
        }
        
        xnd_log_setup_direct(level);
}

void xnd_log_cleanup(void)
{
        for (uint l = 0; l <= log_level; l++) {
                if (l == XND_TRACING) {
                        dprintf(XND_TRACE_FD, "[end of log entry]\n");
                }
                close(level_to_fd[l]);
        }
}

void xnd_log_setup_direct(int level)
{
        int     logfd;
        char    buf[PATH_MAX], path[PATH_MAX];
        u32     size = sizeof(buf);
        
        log_level = level;
        for (int l = 0; l <= min(log_level, XND_DEBUGGING); l++)
                dup2(STDERR_FILENO, level_to_fd[l]);
        
        if (log_level < XND_TRACING) {
                return;
        }

        logfd = open("xnd.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (logfd != -1) {
                xnd_assert(dup2(logfd, XND_TRACE_FD) == XND_TRACE_FD);
                close(logfd);
        }
        
        _NSGetExecutablePath(buf, &size);
        xnd_path_basename(buf, path, sizeof(path));
        xnd_trace("%s Log (%ld):\n", path, (long)time(NULL));
}

void xnd_log_shared_cache_info(void)
{
        char            *dyld_env;
        const void      *base;
        size_t          size;

        dyld_env = getenv("DYLD_SHARED_REGION");
        base = _dyld_get_shared_cache_range(&size);

        xnd_trace("DYLD_SHARED_REGION=%s\n"
                  "dyld shared cache range: %p-%p %zu\n",
                  dyld_env, base, base + size, size);
}

void xnd_log_main_thread_info(void)
{
        uintptr_t tls, main_thread, signature, munge_token;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        main_thread = get_tls_slot(__TSD_THREAD_SELF);
        signature = *(uintptr_t *)main_thread;
        munge_token = get_tls_slot(__TSD_PTR_MUNGE);

        xnd_trace("Main thread info:\n"
                  "        tpiddro_el0: 0x%lx\n"
                  "     pthread_self(): 0x%lx\n"
                  "  pthread signature: 0x%lx\n"
                  "pthread munge token: 0x%lx\n"
                  "  sig ^ munge_token: 0x%lx\n",
                  tls, main_thread, signature, munge_token,
                  signature ^ munge_token);
}
