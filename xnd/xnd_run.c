/* ckpt.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include "xnd/platform/exe.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <spawn.h>
#include <limits.h>

#ifndef POSIX_SPAWN_DISABLE_ASLR
# define POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

static const char *help =
        "OVERVIEW: XND Checkpoint-Restart for MacOS\n\n"
        "USAGE: ./xnd_run [options] file...\n\n"
        "OPTIONS:\n"
        "  -p <file>            Print checkpoint file contents\n"
        "  -c <binary> <args>   Execute binary injected with libxnd.dylib\n"
        "  -r <file>            Restart from saved checkpoint file\n";

static void usage(void)
{
        dprintf(STDERR_FILENO, "%s", help);
}

static __noreturn void print(char *ckptfile)
{
        /* ./xnd_print <ckpt-file> */
        char    xnd_print_path[PATH_MAX];
        int     retval;

        retval = xnd_exe_path_of("xnd_print", xnd_print_path, PATH_MAX);
        if (retval < 0) {
                fprintf(stderr, "Failed to get path of xnd_print!\n");
                exit(EXIT_FAILURE);
        }

        if (execl(xnd_print_path, ckptfile, NULL) < 0)
                err(EXIT_FAILURE, "execl(%s, %s)", xnd_print_path, ckptfile);

        unreachable();
}

static __noreturn void checkpoint(char **argv)
{
        /**
         * DLYD_INSERT_LIBRARIES=libxnd.dylib
         * XND_PROGRAM=<binary>
         * <binary> <args> ...
         */
        char    libxnd_path[PATH_MAX], xnd_program[PATH_MAX];
        int     retval;

        retval = xnd_exe_path_of("libxnd.dylib", libxnd_path, PATH_MAX);
        if (retval < 0) {
                fprintf(stderr, "Failed to get path of libxnd.dylib!\n");
                exit(EXIT_FAILURE);
        }
        
        retval = xnd_path_basename(argv[0], xnd_program, PATH_MAX);
        if (retval < 0) {
                fprintf(stderr, "Failed to parse program name: %s!\n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }

        if (setenv("XND_PROGRAM", xnd_program, 1) < 0 ||
            setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1) < 0)
                err(EXIT_FAILURE, "setenv");
        
        fprintf(stderr, "DYLD_INSERT_LIBRARIES=%s\n" "XND_PROGRAM=%s\n",
                libxnd_path, xnd_program);
        fprintf(stderr, "Executing %s (pid=%d)\n", argv[0], getpid());

        if (execvp(argv[0], argv) < 0)
                err(EXIT_FAILURE, "execvp(%s)", argv[0]);

        unreachable();
}

static __noreturn void restart(char *ckptfile)
{
        short                   flags;
        pid_t                   pid;
        extern char             **environ;
        posix_spawnattr_t       attr;
        char                    xnd_restart_path[PATH_MAX];
        char                    xnd_program[PATH_MAX];
        int                     retval;
        
        retval = xnd_exe_path_of("xnd_restart", xnd_restart_path, PATH_MAX);
        if (retval < 0) {
                fprintf(stderr, "Failed to get path of xnd_restart!\n");
                exit(EXIT_FAILURE);
        }

        posix_spawnattr_init(&attr);
        retval = xnd_ckptfile_parse(ckptfile, xnd_program, PATH_MAX, NULL);
        if (retval < 0) {
                fprintf(stderr, "Failed to extract program name from "
                        "checkpoint file: %s!\n", ckptfile);
                exit(EXIT_FAILURE);
        }

        retval = setenv("XND_PROGRAM", xnd_program, 1);
        if (retval < 0)
                err(EXIT_FAILURE, "setenv");

        fprintf(stderr, "Set XND_PROGRAM=%s\n", xnd_program);

        char *argv[] = { xnd_restart_path, ckptfile, NULL };
        flags = POSIX_SPAWN_DISABLE_ASLR | POSIX_SPAWN_SETEXEC;
        retval = posix_spawnattr_setflags(&attr, flags);
        if (retval < 0)
                err(EXIT_FAILURE, "posix_spawnattr_setflags");

        retval = posix_spawn(&pid, xnd_restart_path, NULL,
                             &attr, argv, environ);
        if (retval < 0) {
                posix_spawnattr_destroy(&attr);
                err(EXIT_FAILURE, "posix_spawn");
        }

        unreachable();
}

int main(int argc, char *argv[])
{
        if (argc < 3) {
                usage();
                exit(0);
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

        exit(0);
}
