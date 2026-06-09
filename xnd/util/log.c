/* log.c */
#include "xnd/xnd.h"
#include "xnd/util/log.h"

#include <stdlib.h>
#include <unistd.h>

static int log_level = XND_DEAFULT_LOG_LEVEL;

static int level_to_fd[] = { 
        XND_ERROR_FD, XND_WARN_FD, XND_DEBUG_FD, XND_TRACE_FD
};

void xnd_log_setup(void)
{
        char *level_str;
        
        level_str = getenv("XND_LOG_LEVEL");
        if (level_str)
                log_level = min(atoi(level_str), XND_MAX_LOG_LEVEL);

        for (int l = 0; l < log_level; l++)
                dup2(STDERR_FILENO, level_to_fd[l]);
}

void xnd_log_cleanup(void)
{
        for (uint l = 0; l < log_level; l++)
                close(level_to_fd[l]);
}

void xnd_log_setup_direct(int level)
{
        log_level = min(level, XND_MAX_LOG_LEVEL);
        for (int l = 0; l < log_level; l++)
                dup2(STDERR_FILENO, level_to_fd[l]);
}
