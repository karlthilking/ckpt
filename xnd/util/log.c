/* log.c */
#include "xnd/xnd.h"
#include "xnd/util/log.h"

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
        if (level_str)
                level = min(atoi(level_str), XND_MAX_LOG_LEVEL);
        else
                level = XND_DEFAULT_LOG_LEVEL;
        
        xnd_log_setup_direct(level);
}

void xnd_log_cleanup(void)
{
        for (uint l = 0; l <= log_level; l++)
                close(level_to_fd[l]);
}

void xnd_log_setup_direct(int level)
{
        int     logfd;
        char    buf[PATH_MAX];
        
        log_level = level;
        for (int l = 0; l <= min(log_level, XND_DEBUGGING); l++)
                dup2(STDERR_FILENO, level_to_fd[l]);
        
        if (log_level < XND_TRACING)
                return;

        logfd = open("xnd.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
        dup2(logfd, XND_TRACE_FD);
        close(logfd);
        
        u32 size = sizeof(buf);
        _NSGetExecutablePath(buf, &size);
        xnd_trace("%s log (%ld):\n", buf, (long)time(NULL));
}
