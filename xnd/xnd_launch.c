/* xnd_launch.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include "xnd/util/log.h"
#include "xnd/platform/exe.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

static void xnd_exit(int);
static void launch(char **);

int main(int argc, char *argv[])
{
        xnd_log_setup();
        if (argc < 2) {
                xnd_error("Usage: ./xnd_launch <binary>...\n");
                xnd_log_cleanup();
                exit(0);
        }
        
        launch(argv + 1);
        exit(0);
}

static __noreturn void xnd_exit(int status)
{
        xnd_log_cleanup();
        exit(status);
}

static __noreturn void launch(char **argv)
{
        /**
         * DLYD_INSERT_LIBRARIES=libxnd.dylib
         * XND_PROGRAM=<binary>
         * <binary> <args> ...
         */
        char    libxnd_path[PATH_MAX], xnd_program[PATH_MAX], pid_str[11];
        int     err;
        pid_t   pid, coord_pid;

        if ((coord_pid = launch_coordinator()) == -1) {
                xnd_error("Failed to launch coordinator\n");
                xnd_exit(XND_EXIT_FAILURE);
        } else {
                snprintf(pid_str, sizeof(pid_str), "%d", coord_pid);
                xnd_assert(setenv("XND_COORD_PID", pid_str, 1) == 0);
                bzero(pid_str, sizeof(pid_str));
        }

        err = xnd_exe_path_of("libxnd.dylib", libxnd_path, PATH_MAX);
        if (err < 0) {
                xnd_error("Failed to get path of libxnd.dylib\n");
                xnd_exit(XND_EXIT_FAILURE);
        }
        
        if (argv[1] && strstr(argv[0], "python")) {
                err = xnd_path_basename(argv[1], xnd_program, PATH_MAX);
                if (err < 0) {
                        xnd_error("xnd_path_basename failed: %s\n", argv[1]);
                        xnd_exit(XND_EXIT_FAILURE);
                }
        } else {
                err = xnd_path_basename(argv[0], xnd_program, PATH_MAX);
                if (err < 0) {
                        xnd_error("xnd_path_basename failed: %s\n", argv[0]);
                        xnd_exit(XND_EXIT_FAILURE);
                }
        }

        err = setenv("XND_PROGRAM", xnd_program, 1);
        if (err != 0) {
                xnd_error("setenv(\"XND_PROGRAM\", %s, 1): %s\n",
                          xnd_program, strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }

        err = setenv("DYLD_INSERT_LIBRARIES", libxnd_path, 1);
        if (err != 0) {
                xnd_error("setenv(\"DYLD_INSERT_LIBRARIES\", %s, 1): %s\n",
                          libxnd_path, strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }

        err = setenv("DYLD_SHARED_REGION", "private", 1);
        if (err != 0) {
                xnd_error("setenv(\"DYLD_SHARED_REGION\", %s, 1): %s\n",
                          "private", strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        }
                           
        xnd_trace("XND_PROGRAM=%s\n"
                  "DYLD_INSERT_LIBRARIES=%s\n"
                  "DYLD_SHARED_REGION=private\n",
                  xnd_program, libxnd_path);
        
        pid = fork();
        if (pid == -1) {
                xnd_error("fork: %s\n", strerror(errno));
                xnd_exit(XND_EXIT_FAILURE);
        } else if (pid == 0) {
                snprintf(pid_str, sizeof(pid_str), "%d", getpid());
                xnd_printf("Executing %s (pid=%s)\n", xnd_program, pid_str);

                err = execvp(argv[0], argv);
                if (err != 0) {
                        xnd_error("execvp: %s\n", strerror(errno));
                        xnd_exit(XND_EXIT_FAILURE);
                }
        } else {
                err = waitpid(pid, NULL, 0);
                if (err < 0) {
                        xnd_error("waitpid: %s\n", strerror(errno));
                        xnd_exit(XND_EXIT_FAILURE);
                }
                xnd_exit(XND_EXIT_SUCCESS);
        }

        unreachable();
}
