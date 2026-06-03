/* ckpt.c */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include "launch.h"

int parse_program_name(char *ckptfile, char *buf, size_t bufsize)
{
        char *start, *end, *p;

        if ((start = strrchr(ckptfile, '/'))) {
                start = start + 1;
        } else {
                start = ckptfile;
        }

        if ((end = strstr(ckptfile, ".ckpt")) == NULL) {
                fprintf(stderr, "Invalid checkpoint file: %s\n", ckptfile);
                return -1;
        }
        
        strncpy(buf, start, min(bufsize, end - start));
        if ((p = strrchr(buf, '-')) == NULL) {
                fprintf(stderr, "Invalid checkpoint file: %s\n", ckptfile);
                return -1;
        }

        *p = '\0';
        return 0;
}

char *strip_path(char *path)
{
        char *s;

        if ((s = strrchr(path, '/'))) {
                return s + 1;
        }
        
        return path;
}

void get_path(const char *file, char *out)
{
        char    buf[PATH_MAX], path[PATH_MAX];
        u32     bufsize = PATH_MAX;
        
        if (_NSGetExecutablePath(buf, &bufsize) < 0) {
                err(EXIT_FAILURE, "_NSGetExecutablePath");
        }

        if (realpath(buf, path) == NULL) {
                err(EXIT_FAILURE, "realpath(%s, ...)", buf);
        }

        strncpy(out, path, strlen(path) + 1);
        strncpy(strstr(strstr(out, "macos"), "ckpt"),
                file, strlen(file) + 1);
}

__noreturn void print(char *ckptfile)
{
        char *argv[3], printckpt_path[PATH_MAX];

        get_path("printckpt", printckpt_path);
        
        argv[0] = printckpt_path;
        argv[1] = ckptfile;
        argv[2] = NULL;

        if (execvp(argv[0], argv) < 0) {
                err(EXIT_FAILURE, "execvp(%s, ...)", argv[0]);
        }

        unreachable();
}

__noreturn void checkpoint(char **argv)
{
        int     ret;
        char    libckpt_path[PATH_MAX], *prog;

        get_path("libckpt.dylib", libckpt_path);
        ret = setenv("DYLD_INSERT_LIBRARIES", libckpt_path, 1);
        if (ret < 0) {
                err(EXIT_FAILURE, "setenv");
        }
        
        prog = strip_path(argv[0]);
        ret = setenv("LIBCKPT_PROGRAM", prog, 1);
        if (ret < 0) {
                err(EXIT_FAILURE, "setenv");
        }
        fprintf(stderr, "Set LIBCKPT_PROGRAM=%s\n", prog);
        
        fprintf(stderr, "Executing %s (pid=%d)\n", argv[0], getpid());
        if (execvp(argv[0], argv) < 0) {
                err(EXIT_FAILURE, "execvp(%s, ...)", argv[0]);
        }

        unreachable();
}

__noreturn void restart(char *ckptfile)
{
        int                     retval;
        short                   flags;
        pid_t                   pid;
        extern char             **environ;
        posix_spawnattr_t       attr;
        char                    restart_path[PATH_MAX], *argv[3];

        get_path("restart", restart_path);
        posix_spawnattr_init(&attr);

        /**
         * Checkpoint File Name:
         * <program>-<time>-<pid>.ckpt
         * So, environment variable LIBCKPT_PROGRAM should be set to
         * <program> before restarting.
         */
        {
                char prog[128];
                
                retval = parse_program_name(ckptfile, prog, sizeof(prog));
                if (retval < 0) {
                        fprintf(stderr, "Failed to get program name\n");
                        exit(EXIT_FAILURE);
                }
                
                retval = setenv("LIBCKPT_PROGRAM", prog, 1);
                if (retval < 0) {
                        err(EXIT_FAILURE, "setenv");
                }

                fprintf(stderr, "Set LIBCKPT_PROGRAM=%s\n", prog);
        }
        
        argv[0] = restart_path;
        argv[1] = ckptfile;
        argv[2] = NULL;

        /**
         * Disable address space layout randomization so fixed 
         * restart text and data segments are not slid
         */
        flags = POSIX_SPAWN_DISABLE_ASLR | POSIX_SPAWN_SETEXEC;
        retval = posix_spawnattr_setflags(&attr, flags);
        if (retval < 0) {
                err(EXIT_FAILURE, "posix_spawnattr_setflags");
        }
        
        retval = posix_spawn(&pid, restart_path, NULL,
                             &attr, argv, environ);
        if (retval < 0) {
                posix_spawnattr_destroy(&attr);
                err(EXIT_FAILURE, "posix_spawn(..., %s, ...)",
                    restart_path);
        }

        unreachable();
}

void usage(void)
{
        fprintf(stderr,
"OVERVIEW: MacOS Checkpoint-Restart\n\n"
"USAGE: ./ckpt [options] file...\n\n"
"OPTIONS:\n"
"  -p <file>            Print checkpoint file contents\n"
"  -c <binary> <args>   Execute binary injected with libckpt.dylib\n"
"  -r <file>            Restart from saved checkpoint file\n"
"  -i                   Launch interactive mode\n\n");
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                usage();
                exit(EXIT_FAILURE);
        } else if (argc == 2) {
                if (strncmp(argv[1], "-i", 2) == 0) {
                        interactive();
                }
                usage();
                exit(EXIT_FAILURE);
        }
        
        switch (getopt(argc, argv, "c:r:p:")) {
        case 'c':
                checkpoint(&argv[optind - 1]);
        case 'r':
                restart(optarg);
        case 'p':
                print(optarg);
        case '?':
        default:
                usage();
        }

        return 0;
}
