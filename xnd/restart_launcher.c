/* restart_launcher.c */
#define _XOPEN_SOURCE
#include "xnd/xnd.h"
#include "xnd/xnd_restart.h"
#include "util/env.h"
#include "util/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <spawn.h>

extern char **environ;

int main(int argc, char *argv[])
{
        int                     err;
        pid_t                   pid;
        short                   flags;
        posix_spawnattr_t       attr;
        
        xnd_assert(setenv("DYLD_SHARED_REGION", "private", 1) == 0);

        posix_spawnattr_init(&attr);
        flags = POSIX_SPAWN_SETEXEC | POSIX_SPAWN_DISABLE_ASLR;
        if ((err = posix_spawnattr_setflags(&attr, flags)) != 0) {
                xnd_error("posix_spawnattr_setflags: %s\n",
                          strerror(err));
                exit(XND_EXIT_FAILURE);
        }

        char *args[] = { "xnd_restart", argv[1] , NULL };
        err = posix_spawn(&pid, "xnd_restart", NULL, &attr, args, environ);
        if (err != 0) {
                posix_spawnattr_destroy(&attr);
                xnd_error("posix_spawn: %s\n", strerror(err));
                exit(XND_EXIT_FAILURE);
        }

        unreachable();
}
