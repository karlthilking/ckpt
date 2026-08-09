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
#define ARG_IS_UNLINK_TMP(arg) \
        (strncmp(arg, "--unlink-tmp", sizeof("--unlink-tmp")) == 0)
#define ARG_IS_INVALID(arg) \
        ((strncmp(arg, "--", 2) == 0) || \
         (strlen(arg) == 2 && (arg)[0] == '-'))

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
"  --unlink-tmp [0 | 1]\n"
"       Unlink the temporary arm64 executable created by xnd_launch\n"
"       (Only relevant when xnd_launch is given an arm64e executable)\n"
"        Default: 1 (true)\n\n"
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
                } else if (ARG_IS_UNLINK_TMP(argv[0])) {
                        env_set_unlink_tmp_binary(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_IS_INVALID(argv[0])) {
                        xnd_printf("Invalid argument: %s\n", argv[0]);
                        usage();
                        xnd_exit(XND_EXIT_FAILURE);
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
        int    ret;
        char   exec_path[PATH_MAX], tmp[PATH_MAX] = { '.', '/', 0 };

        new_argv[argc] = NULL;
        for (uint i = 1u; i < argc; i++)
                new_argv[i] = strdup(argv[i]);

	ret = xnd_path_basename(&tmp[2], sizeof(tmp) - 2, argv[0]);
        if (ret != 0) {
                xnd_error("Failed to get basename of %s\n", argv[0]);
                xnd_exit(XND_EXIT_FAILURE);
        } else {
                strcat(tmp, "-tmp");
        }

        if (access(argv[0], X_OK) == 0) {
                strncpy(exec_path, argv[0], strlen(argv[0]) + 1);
        } else {
                ret = xnd_path_find(argv[0], exec_path, PATH_MAX);
                if (ret != 0) {
                        xnd_error("Failed to find path of %s\n", argv[0]);
                        xnd_exit(XND_EXIT_FAILURE);
                }
        }

        ret = binary_arm64e_to_arm64(exec_path, tmp);
        switch (ret) {
        case ARM64E_TO_ARM64_SUCCESS:
                new_argv[0] = strdup(tmp);
                env_set_tmp_binary(tmp);
                break;
        case ARM64E_TO_ARM64_NOT_ARM64E:
                new_argv[0] = strdup(exec_path);
                break;
        case ARM64E_TO_ARM64_FAILURE:
                xnd_error("Error patching arm64e binary: %s\n", exec_path);
                xnd_exit(XND_EXIT_FAILURE);
        default:
                xnd_error("Unknown binary_arm64e_to_arm64 return value\n");
                xnd_exit(XND_EXIT_FAILURE);
        }
}

static __noreturn void launch(int argc, char **argv)
{
        char    libxnd_path[PATH_MAX];
        char    *new_argv[argc + 1];
        int     err;
        pid_t   coord_pid;

        prepare_args(argc, argv, new_argv);
        if ((coord_pid = launch_coordinator(false)) == -1) {
                xnd_error("Failed to launch coordinator\n");
                xnd_exit(XND_EXIT_FAILURE);
        }

	err = xnd_exe_path_of(libxnd_path, PATH_MAX, "libxnd.dylib");
        if (err < 0) {
                xnd_error("Failed to get path of libxnd.dylib\n");
                xnd_exit(XND_EXIT_FAILURE);
        }

        env_set_program_name(new_argv[0]);
        xnd_assert(setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1) == 0);

        env_set_dyld_shared_region_private();
        xnd_trace("XND_PROGRAM=%s\n"
                  "DYLD_INSERT_LIBRARIES=%s\n"
                  "DYLD_SHARED_REGION=private\n",
                  new_argv[0], libxnd_path);

        xnd_printf("Executing %s (pid=%d)\n", new_argv[0], getpid());
        err = execvp(new_argv[0], new_argv);
        if (err != 0) {
                xnd_error("execvp(%s): %s\n",
                          new_argv[0], strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }

        unreachable();
}
