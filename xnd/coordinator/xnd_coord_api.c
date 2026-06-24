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
#include <string.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/select.h>

static int coord_fd = -1;

pid_t launch_coordinator(void)
{
        char    buf[PATH_MAX], exe[PATH_MAX];
        pid_t   coord_pid;
        int     err;

        coord_pid = fork();
        switch (coord_pid) {
        case -1:
                xnd_error("fork: %s\n", strerror(errno));
                return -1;
        case 0:
                xnd_exe_dir(buf, sizeof(buf));
                xnd_path_join(exe, sizeof(exe), buf, "xnd_coordinator");
                err = execl(exe, exe, NULL);
                if (err != 0) {
                        xnd_error("execl(%s): %s\n", exe, strerror(errno));
                        exit(XND_EXIT_FAILURE);
                }
        default:
                break;
        }

        snprintf(buf, sizeof(buf), "%d", coord_pid);
        xnd_assert(setenv("XND_COORD_PID", buf, 1) == 0);

        return coord_pid;
}

void connect_to_coord(void)
{
        int                     err, tries, fd;
        struct sockaddr_un      addr;
        struct timespec         ts = { 0, 10 * 1000000 };

        coord_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (coord_fd < 0) {
                xnd_error("socket: %s\n", strerror(errno));
                xnd_abort();
        }

        if (likely(coord_fd != XND_COORD_FD)) {
                fd = dup2(coord_fd, XND_COORD_FD);
                if (fd < 0) {
                        xnd_error("dup2: %s\n", strerror(errno));
                        xnd_abort();
                }
                close(coord_fd);
                coord_fd = fd;
                xnd_trace("coord_fd=%d\n", coord_fd);
        }
        
        bzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path) - 1);

        tries = 0;
again:
        err = connect(coord_fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0) {
                if (errno == ECONNREFUSED && tries++ < 100) {
                        nanosleep(&ts, NULL);
                        goto again;
                } else {
                        xnd_error("connect: %s\n", strerror(errno));
                        xnd_abort();
                }
        }

        xnd_trace("Connected with coordinator (path: %s)\n", XND_COORD_PATH);
}

void disconnect_from_coord(void)
{
        if (coord_fd != -1) {
                close(coord_fd);
        }
}

pid_t recv_virt_to_real_pid(pid_t virt)
{
        struct xnd_msg  msg;
        int             err;

        msg.hdr = XND_VIRT_TO_REAL;
        msg.virt_pid = virt;
        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to send request to coordinator\n");
                return -1;
        }
        
        err = recv_msg_from_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to receive response from coordinator\n");
                return -1;
        }

        if (msg.hdr != XND_COORD_ACK || msg.ret != XND_SUCCESS) {
                xnd_error("Virtual to real pid translation failed\n");
                return -1;
        }
        
        return msg.real_pid;
}

pid_t recv_real_to_virt_pid(pid_t real)
{
        struct xnd_msg  msg;
        int             err;

        msg.hdr = XND_REAL_TO_VIRT;
        msg.real_pid = real;
        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to send request to coordinator\n");
                return -1;
        }

        err = recv_msg_from_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to receive response from coordinator\n");
                return -1;
        }

        if (msg.hdr != XND_COORD_ACK || msg.ret != XND_SUCCESS) {
                xnd_error("Real to virtual pid translation failed!\n");
                return -1;
        }

        return msg.virt_pid;
}

int send_msg_to_coord(struct xnd_msg *msg)
{
        ssize_t bytes;

        bytes = writeall(coord_fd, msg, sizeof(struct xnd_msg));
        if (bytes != sizeof(struct xnd_msg)) {
                return -1;
        }

        return 0;
}

int recv_msg_from_coord(struct xnd_msg *msg)
{
        ssize_t bytes;

        bytes = readall(coord_fd, msg, sizeof(struct xnd_msg));
        if (bytes != sizeof(struct xnd_msg)) {
                return -1;
        }

        return 0;
}

int check_coord_status(void)
{
        int             err, stat;
        socklen_t       len = sizeof(stat);
        
        err = getsockopt(coord_fd, SOL_SOCKET, SO_ERROR, &stat, &len);
        if (err != 0) {
                xnd_error("getsockopt: %s\n", strerror(errno));
                return -1;
        }

        return stat;
}
