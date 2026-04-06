/* ckpt.c */
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include "../include/ckpt.h"

#ifndef POSIX_SPAWN_DISABLE_ASLR
#define POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

int restart(char *ckptfile)
{
        int                     rc, wstat;
        short                   flags;
        pid_t                   pid;
        char                    *args[3] = {"./restart", ckptfile, NULL};
        extern char             **environ;
        posix_spawnattr_t       attr;

        posix_spawnattr_init(&attr);
        flags = POSIX_SPAWN_DISABLE_ASLR;
        
        rc = posix_spawnattr_setflags(&attr, flags);
        if (rc < 0) {
                perror("posix_spawnattr_setflags");
                return -1;
        }
        
        rc = posix_spawn(&pid, "./restart", NULL, &attr, args, environ);
        posix_spawnattr_destroy(&attr);
        if (rc < 0) {
                perror("posix_spawn");
                return -1;
        }

        waitpid(pid, &wstat, 0);

        if (WIFEXITED(wstat) && WEXITSTATUS(wstat) != 0) {
                fprintf(stderr, 
                        "%d exited with return code %d\n",
                        pid, WEXITSTATUS(wstat));
                return -1;
        }
        
        exit(0);
}

int main(int argc, char *argv[])
{
        if (argc < 3) {
                fprintf(stderr,
                        "Usage: ./ckpt [options] [file] [args]\n"
                        "  -p [ckpt-file]          "
                        "Print checkpoint file contents\n"
                        "  -c [executable] [args]  "
                        "Run and inject executable with "
                        "libckpt.dylib\n"
                        "  -r [ckpt-file]          "
                        "Restart process from checkpoint file\n");
                exit(1);
        }

        if (strncmp(argv[1], "-p", 2) == 0 &&
            execl("./printckpt", "./printckpt", argv[2], NULL) < 0)
                err(EXIT_FAILURE, "execl");
        else if (strncmp(argv[1], "-r", 2) == 0 &&
                 restart(argv[2]) < 0)
                exit(EXIT_FAILURE);
        else if (!strncmp(argv[1], "-c", 2)) {
                if (setenv("DYLD_INSERT_LIBRARIES",
                           "./libckpt.dylib", 1) < 0)
                        err(EXIT_FAILURE, "setenv");
                printf("Executing %s (pid=%d)\n", argv[1], getpid());
                if (execvp(argv[2], argv + 2) < 0)
                        err(EXIT_FAILURE, "execvp");
        } else {
                fprintf(stderr, "Unrecognized option: %s\n", argv[1]);
                exit(EXIT_FAILURE);
        }
}
