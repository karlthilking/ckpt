/* ckpt.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include "xnd/util/log.h"
#include "xnd/platform/exe.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
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
static void launch_coordinator(void);
static void print_checkpoint(char **);
static void launch_checkpoint_target(char **);
static void restart_from_checkpoint(char *);

int main(int argc, char *argv[])
{
        xnd_log_setup();
        if (argc < 3) {
                usage();
                xnd_log_cleanup();
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
        
        xnd_log_cleanup();
        usage();
        exit(0);
}

static void usage(void)
{
        xnd_error("%s", help);
}

static void launch_coordinator(void)
{
        char    buf[PATH_MAX], exe[PATH_MAX];
        pid_t   coord_pid;
        int     err;

        coord_pid = fork();
        switch (coord_pid) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                xnd_abort();
        case 0:
                xnd_exe_dir(buf, sizeof(buf));
                xnd_path_join(exe, sizeof(exe), buf, "xnd_coordinator");
                err = execl(exe, exe, NULL);
                if (err != 0) {
                        xnd_error("execl(%s, ...): %s\n",
                                  exe, strerror(errno));
                        xnd_abort();
                }
        default:
                snprintf(buf, sizeof(buf), "%d", coord_pid);
                xnd_assert(setenv("XND_COORD_PID", buf, 1) == 0);
                break;
        }
}

static __noreturn void print_checkpoint(char **argv)
{
        /* ./xnd_print <ckpt-file> */
        char    xnd_print_path[PATH_MAX];
        int     retval, idx;
        char    *xnd_print_args[sysconf(_SC_ARG_MAX) / PATH_MAX];

        retval = xnd_exe_path_of("xnd_print", xnd_print_path, PATH_MAX);
        if (retval < 0) {
                xnd_error("Failed to get path of xnd_print\n");
                exit(XND_EXIT_FAILURE);
        }
        
        xnd_print_args[0] = xnd_print_path;
        for (idx = 1; argv[idx - 1]; idx++) {
                xnd_print_args[idx] = argv[idx - 1];
        }
        xnd_print_args[idx] = NULL;

        if (execvp(xnd_print_args[0], (char **)xnd_print_args) < 0) {
                xnd_error("execvp(%s, {%s, %s, ...}: %s\n",
                          xnd_print_args[0], xnd_print_args[0],
                          xnd_print_args[1], strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

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

        launch_coordinator();

        retval = xnd_exe_path_of("libxnd.dylib", libxnd_path, PATH_MAX);
        if (retval < 0) {
                xnd_error("Failed to get path of libxnd.dylib\n");
                exit(XND_EXIT_FAILURE);
        }
        
        retval = xnd_path_basename(argv[0], xnd_program, PATH_MAX);
        if (retval < 0) {
                xnd_error("Failed to parse program name: %s\n", argv[0]);
                exit(XND_EXIT_FAILURE);
        }
        
        retval = setenv("XND_PROGRAM", xnd_program, 1);
        if (retval != 0) {
                xnd_error("setenv(\"XND_PROGRAM\", %s, 1): %s\n",
                          xnd_program, strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

        retval = setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1);
        if (retval != 0) {
                xnd_error("setenv(\"DYLD_INSERT_LIBRARIES\", %s, 1): %s\n",
                          libxnd_path, strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

        retval = setenv("DYLD_SHARED_REGION", "private", 1);
        if (retval != 0) {
                xnd_error("setenv(\"DYLD_SHARED_REGION\", %s, 1): %s\n",
                          "private", strerror(errno));
                exit(XND_EXIT_FAILURE);
        }
                           
        xnd_trace("XND_PROGRAM=%s\n"
                  "DYLD_INSERT_LIBRARIES=%s\n"
                  "DYLD_SHARED_REGION=private\n",
                  xnd_program, libxnd_path);
        
        xnd_printf("Executing %s (pid=%d)\n", argv[0], getpid());
        retval = execvp(argv[0], argv);
        if (retval != 0) {
                xnd_error("execvp(%s, ...): %s\n", argv[0], strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

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
        
        launch_coordinator();
        retval = xnd_exe_path_of("xnd_restart", xnd_restart_path, PATH_MAX);
        if (retval < 0) {
                xnd_error("Failed to get path of xnd_restart\n");
                exit(XND_EXIT_FAILURE);
        }

        posix_spawnattr_init(&attr);
        retval = xnd_ckptfile_parse(ckptfile, xnd_program, PATH_MAX, NULL);
        if (retval < 0) {
                xnd_error("Failed to extract program name from checkpoint "
                          "failed: %s\n", ckptfile);
                exit(XND_EXIT_FAILURE);
        }

        retval = setenv("XND_PROGRAM", xnd_program, 1);
        if (retval != 0) {
                xnd_error("setenv(\"XND_PROGRAM\", %s, 1): %s\n",
                          xnd_program, strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

        retval = setenv("DYLD_SHARED_REGION", "private", 1);
        if (retval != 0) {
                xnd_error("setenv(\"DYLD_SHARED_REGION\", %s, 1): %s\n",
                          "private", strerror(errno));
                exit(XND_EXIT_FAILURE);
        }
        
        xnd_trace("XND_PROGRAM=%s\n"
                  "DYLD_SHARED_REGION=private\n",
                  xnd_program);

        char *argv[] = { xnd_restart_path, ckptfile, NULL };
        flags = POSIX_SPAWN_DISABLE_ASLR | POSIX_SPAWN_SETEXEC;

        retval = posix_spawnattr_setflags(&attr, flags);
        if (retval != 0) {
                xnd_error("posix_spawnattr_setflags: %s\n", strerror(retval));
                exit(XND_EXIT_FAILURE);
        }
        
        retval = posix_spawn(&pid, xnd_restart_path, NULL,
                             &attr, argv, environ);
        if (retval != 0) {
                posix_spawnattr_destroy(&attr);
                xnd_error("posix_spawn: %s\n", strerror(retval));
                exit(XND_EXIT_FAILURE);
        }
        
        unreachable();
}
