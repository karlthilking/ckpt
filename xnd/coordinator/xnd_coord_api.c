/* xnd_coord_api.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/pid/pid.h"
#include "xnd/util/io.h"
#include "xnd/util/path.h"
#include "xnd/platform/exe.h"
#include "xnd/pid/pid_table.h"
#include "xnd/coordinator/xnd_coord_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/select.h>

pid_t launch_coordinator(bool restarting)
{
        char    buf[PATH_MAX], exe[PATH_MAX];
        pid_t   coord_pid;
        int     err;

        coord_pid = fork();
        switch (coord_pid) {
        case -1: {
                xnd_error("fork: %s\n", strerror(errno));
                return -1;
        }
        case 0: {
                xnd_exe_dir(buf, sizeof(buf));
                xnd_path_join(exe, sizeof(exe), buf, "xnd_coordinator");

                if (restarting) {
                        err = execl(exe, exe, XND_COORD_RESTART_FLAG, NULL);
                } else {
                        err = execl(exe, exe, NULL);
                }

                if (err != 0) {
                        xnd_error("execl(%s): %s\n", exe, strerror(errno));
                        exit(XND_EXIT_FAILURE);
                }
        }
        default:
                break;
        }
        
        snprintf(buf, sizeof(buf), "%d", coord_pid);
        xnd_assert(setenv("XND_COORD_PID", buf, 1) == 0);

        return coord_pid;
}

pid_t get_coord_pid(void)
{
        static pid_t    coord_pid = -1;
        char            *pid_str = NULL;

        if (coord_pid == -1) {
                if ((pid_str = getenv("XND_COORD_PID")) != NULL) {
                        coord_pid = atoi(pid_str);
                }
        }

        return coord_pid;
}

int connect_to_coord(void)
{
        int                     fd, err, tries;
        struct sockaddr_un      addr;
        struct timespec         ts = { 0, 10 * 100000 };

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
                xnd_error("socket: %s\n", strerror(errno));
                xnd_abort();
        }

        bzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path) - 1);

        tries = 0;
again:
        err = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0) {
                if (errno == ECONNREFUSED) {
                        xnd_assert(tries++ < 100);
                        nanosleep(&ts, NULL);
                        goto again;
                } else {
                        xnd_error("connect: %s\n", strerror(errno));
                        xnd_abort();
                }
        }

        xnd_trace("Connect with coordinator (path: %s, fd: %d)\n",
                  XND_COORD_PATH, fd);

        return fd;
}

int send_command_to_coord(enum xnd_cmd cmd)
{
        int             fd, err;
        struct xnd_msg  msg;
        
        msg.hdr = XND_COMMAND;
        msg.cmd = cmd;
        fd = connect_to_coord();
        
        err = send_msg_to_coord(fd, &msg);
        if (err != 0) {
                xnd_error("Failed to send command to coordinator\n");
                goto out;
        }

        err = recv_msg_from_coord(fd, &msg);
        if (err != 0) {
                xnd_error("Failed to receive ack from coordinator\n");
                goto out;
        }

        xnd_assert(msg.hdr == XND_COORD_ACK && msg.ret == XND_SUCCESS);
out:
        close(fd);
        return err;
}

int send_msg_to_coord(int fd, struct xnd_msg *msg)
{
        ssize_t bytes;

#if DEVELOPMENT || DEBUG
        xnd_trace("Sending message to coordinator: %s\n",
                  xnd_msghdr_string(msg->hdr));
#endif

        xnd_assert(fd != -1);
        bytes = writeall(fd, msg, sizeof(struct xnd_msg));
        if (bytes != sizeof(struct xnd_msg)) {
                return -1;
        }

        return 0;
}

int recv_msg_from_coord(int fd, struct xnd_msg *msg)
{
        ssize_t bytes;

        xnd_assert(fd != -1);
        bytes = readall(fd, msg, sizeof(struct xnd_msg));
        if (bytes != sizeof(struct xnd_msg)) {
                return -1;
        }

#if DEVELOPMENT || DEBUG
        xnd_trace("Received message from coordinator: %s\n",
                  xnd_msghdr_string(msg->hdr));
#endif

        return 0;
}

bool coord_exited(int fd)
{
        int     err, flags;
        pid_t   coord_pid;
        char    *coord_pid_str, buf[32];
        bool    exited;

        if ((coord_pid_str = getenv("XND_COORD_PID")) != NULL) {
                coord_pid = atoi(coord_pid_str);
                err = kill(coord_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        return true;
                }
        }

        if ((flags = fcntl(fd, F_GETFL)) == -1 ||
             fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                return true;
        }

        if (recv(fd, buf, sizeof(buf), MSG_PEEK) == 0) {
                exited = true;
        } else {
                exited = false;
        }

        if ((flags = fcntl(fd, F_GETFL)) == -1 ||
             fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
                return true;
        }

        return exited;
}

int coord_socket_status(int fd)
{
        int             err, stat;
        socklen_t       len = sizeof(stat);

        xnd_assert(fd != -1);
        err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &stat, &len);
        if (err != 0) {
                xnd_error("getsockopt: %s\n", strerror(errno));
                return -1;
        }

        return stat;
}
