/* xnd_launch.c */
#include "xnd/xnd.h"
#include "ckptfile.h"
#include "util/path.h"
#include "util/log.h"
#include "util/env.h"
#include "platform/exe.h"
#include "platform/macho.h"
#include "coordinator/xnd_coord_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define ARG_IS_HELP(arg) \
        (strncmp(arg, "--help", sizeof("--help")) == 0)
#define ARG_IS_CKPT_INTERVAL(arg) \
        (strncmp(arg, "-i", sizeof("-i")) == 0 || \
         strncmp(arg, "--interval", sizeof("--interval")) == 0)
#define ARG_IS_USE_ZLIB(arg) \
        (strncmp(arg, "--use-zlib", sizeof("--use-zlib")) == 0)
#define ARG_IS_NO_ZLIB(arg) \
        (strncmp(arg, "--no-zlib", sizeof("no-zlib")) == 0)
#define ARG_IS_CKPT_SIGNAL(arg) \
        (strncmp(arg, "--ckpt-signal", sizeof("--ckpt-signal")) == 0)

static void usage(void);
static void xnd_exit(int);
static void prepare_args(int, char **, char **);
static void launch(int, char **);

static const char *help =
"OVERVIEW: xnd_launch\n\n"
"DESCRIPTION: Launch computation under XND\n\n"
"USAGE: ./xnd_launch [options] <binary> [args...]\n\n"
"OPTIONS:\n\n"
"  -i, --interval <seconds>\n"
"       Set checkpoint frequency in seconds\n"
"       Default: 0 (manual checkpoints only)\n\n"
"  --use-zlib\n"
"       Use zlib for checkpoint file compression\n\n"
"  --no-zlib\n"
"       Don't use zlib for checkpoint file compression\n\n"
"  --ckpt-signal <signal>\n"
"       Set the signal used for checkpoints\n"
"       Default: SIGUSR2\n\n"
"  --help\n"
"       Display this help message\n";

int main(int argc, char *argv[])
{
        xnd_log_setup();
        if (argc < 2) {
                usage();
                xnd_exit(XND_EXIT_FAILURE);
        }

        argv++;
        argc--;
        for (;;) {
                if (ARG_IS_HELP(argv[0])) {
                        usage();
                        xnd_exit(XND_EXIT_SUCCESS);
                } else if (ARG_IS_CKPT_INTERVAL(argv[0])) {
                        env_set_ckpt_interval(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_IS_USE_ZLIB(argv[0])) {
                        env_set_zlib_compression("1");
                        argv++;
                        argc--;
                } else if (ARG_IS_NO_ZLIB(argv[0])) {
                        env_set_zlib_compression("0");
                        argv++;
                        argc--;
                } else if (ARG_IS_CKPT_SIGNAL(argv[0])) {
                        env_set_ckpt_signal(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else {
                        break;
                }
        }

        launch(argc, argv);
}

static void usage(void)
{
        xnd_printf("%s", help);
}

static __noreturn void xnd_exit(int status)
{
        xnd_log_cleanup();
        exit(status);
}

static void prepare_args(int argc, char **argv, char **new_argv)
{
        int     err;
        char    progname[PATH_MAX];
        
        bzero(progname, sizeof(progname));
        for (int i = 1; i < argc; i++) {
                new_argv[i] = strdup(argv[i]);
        }
        new_argv[argc] = NULL;
        
        strncpy(progname, "./", 2);
        err = xnd_path_basename(argv[0], progname + 2, PATH_MAX - 2);
        if (err < 0) {
                xnd_error("xnd_path_basename failed: %s\n", argv[0]);
                xnd_exit(XND_EXIT_FAILURE);
        }

        strcat(progname, "_tmp");
        if (binary_arm64e_to_arm64(argv[0], progname)) {
                new_argv[0] = strdup(progname);
                env_set_tmp_binary(progname);
        } else {
                new_argv[0] = strdup(argv[0]);
        }
}

static __noreturn void launch(int argc, char **argv)
{
        /**
         * DLYD_INSERT_LIBRARIES=libxnd.dylib
         * XND_PROGRAM=<binary>
         * <binary> <args> ...
         */
        char    libxnd_path[PATH_MAX], xnd_program[PATH_MAX];
        char    *new_argv[argc + 1];
        int     err;
        pid_t   coord_pid;
        
        prepare_args(argc, argv, new_argv);
        if ((coord_pid = launch_coordinator(false)) == -1) {
                xnd_error("Failed to launch coordinator\n");
                xnd_exit(XND_EXIT_FAILURE);
        }

        err = xnd_exe_path_of("libxnd.dylib", libxnd_path, PATH_MAX);
        if (err < 0) {
                xnd_error("Failed to get path of libxnd.dylib\n");
                xnd_exit(XND_EXIT_FAILURE);
        }

        err = xnd_path_basename(argv[0], xnd_program, PATH_MAX);
        if (err < 0) {
                xnd_error("xnd_path_basename failed: %s\n", argv[0]);
                xnd_exit(XND_EXIT_FAILURE);
        }
        
        env_set_program_name(xnd_program);
        err = setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1);
        if (err != 0) {
                xnd_error("setenv(\"DYLD_INSERT_LIBRARIES\", %s, 1): %s\n",
                          libxnd_path, strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }

        env_set_dyld_shared_region_private();
        xnd_trace("XND_PROGRAM=%s\n"
                  "DYLD_INSERT_LIBRARIES=%s\n"
                  "DYLD_SHARED_REGION=private\n",
                  xnd_program, libxnd_path);
        
        xnd_printf("Executing %s (pid=%d)\n", xnd_program, getpid());
        err = execvp(new_argv[0], new_argv);
        if (err != 0) {
                xnd_error("execvp(%s): %s\n", new_argv[0], strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }
        
        xnd_exit(XND_EXIT_SUCCESS);
        unreachable();
}
