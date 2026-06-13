/* ckpt.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include "xnd/platform/exe.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <spawn.h>
#include <limits.h>

#ifndef POSIX_SPAWN_DISABLE_ASLR
# define POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

static const char *help =
"OVERVIEW: xnd_run\n\n"
"DESCRIPTION: Checkpoint-restart for MacOS (arm64)\n\n"
"USAGE: ./xnd_run [options] file ...\n\n"
"OPTIONS:\n"
" --launch, -l <binary> <args>\n"
"    Execute a binary injected with libxnd.dylib. The spawned process can\n"
"    be checkpointed throughout its execution.\n\n"
" --restart, -r <checkpoint-file>\n"
"    Restart a program from a previous checkpoint using the checkpoint\n"
"    image file that was created as a result of the checkpoint. The \n"
"    restart process can also receive subsequent checkpoints.\n\n"
" --print, -p <checkpoint-file>\n"
"    Invoke the xnd_print utility in order to display the contents of a\n"
"    checkpoint file in a human-readable way. The xnd_print program will\n"
"    display information about each memory region as well as the register\n"
"    context that was serialized to the checkpoint image file.\n";

static void usage(void);
static void print_checkpoint(char **);
static void launch_checkpoint_target(char **);
static void restart_from_checkpoint(char *);

int main(int argc, char *argv[])
{
        if (argc < 3) {
                usage();
                exit(0);
        }
        
        switch (getopt(argc, argv, "l:r:p:")) {
        case 'l':
                launch_checkpoint_target(&argv[optind - 1]);
        case 'r':
                restart_from_checkpoint(optarg);
        case 'p':
                print_checkpoint(&argv[optind - 1]);
        case '?':
        default:
                break;
        }
        
        usage();
        exit(0);
}

static void usage(void)
{
        fprintf(stderr, "%s", help);
}

static __noreturn void print_checkpoint(char **argv)
{
        /* ./xnd_print <ckpt-file> */
        char    xnd_print_path[PATH_MAX];
        int     retval, idx;
        char    *xnd_print_args[sysconf(_SC_ARG_MAX) / PATH_MAX];

        retval = xnd_exe_path_of("xnd_print", xnd_print_path, PATH_MAX);
        if (retval < 0) {
                fprintf(stderr, "Failed to get path of xnd_print!\n");
                exit(EXIT_FAILURE);
        }
        
        xnd_print_args[0] = xnd_print_path;
        for (idx = 1; argv[idx - 1]; idx++)
                xnd_print_args[idx] = argv[idx - 1];
        xnd_print_args[idx] = NULL;

        if (execvp(xnd_print_args[0], (char **)xnd_print_args) < 0)
                err(EXIT_FAILURE, "execvp(%s, {%s, %s, ...})",
                    xnd_print_args[0], xnd_print_args[0], xnd_print_args[1]);

        unreachable();
}

static __noreturn void launch_checkpoint_target(char **argv)
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
            setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1) < 0 ||
            setenv("DYLD_SHARED_REGION", "private", 1) < 0)
                err(EXIT_FAILURE, "setenv");

        fprintf(stderr, "XND_PROGRAM=%s\n"
                "DYLD_INSERT_LIBRARIES=%s\n"
                "DYLD_SHARED_REGION=private\n",
                xnd_program, libxnd_path);
        
        fprintf(stderr, "Executing %s (pid=%d)\n", argv[0], getpid());
        if (execvp(argv[0], argv) < 0)
                err(EXIT_FAILURE, "execvp(%s)", argv[0]);

        unreachable();
}

static __noreturn void restart_from_checkpoint(char *ckptfile)
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
        
        if (setenv("XND_PROGRAM", xnd_program, 1) < 0 ||
            setenv("DYLD_SHARED_REGION", "private", 1) < 0)
                err(EXIT_FAILURE, "setenv");

        fprintf(stderr, "Set XND_PROGRAM=%s\n", xnd_program);

        char *argv[] = { xnd_restart_path, ckptfile, NULL };
        flags = POSIX_SPAWN_DISABLE_ASLR | POSIX_SPAWN_SETEXEC;
        retval = posix_spawnattr_setflags(&attr, flags);
        if (retval < 0)
                err(EXIT_FAILURE, "posix_spawnattr_setflags");

        retval = posix_spawn(&pid, xnd_restart_path, NULL,
                             &attr, argv, environ);
        if (retval != 0) {
                posix_spawnattr_destroy(&attr);
                fprintf(stderr, "posix_spawn: %s\n", strerror(retval));
                exit(EXIT_FAILURE);
        }

        unreachable();
}
