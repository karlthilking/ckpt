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

void connect_to_coord(void)
{
        int                     err, tries;
        struct sockaddr_un      addr;
        struct timespec         ts = { 0, 10 * 1000000 };

        coord_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (coord_fd < 0) {
                xnd_error("socket: %s\n", strerror(errno));
                xnd_abort();
        }
        
        bzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path) - 1);
        
        tries = 0;
again:
        err = connect(coord_fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0 && errno == ECONNREFUSED) {
                if (tries++ < 100) {
                        nanosleep(&ts, NULL);
                        goto again;
                } else {
                        xnd_error("connect: %s\n", strerror(errno));
                        xnd_abort();
                }
        } else if (err != 0) {
                xnd_error("connect: %s\n", strerror(errno));
                xnd_abort();
        }

        xnd_trace("Connected with coordinator (path: %s)\n", XND_COORD_PATH);
}

void disconnect_from_coord(void)
{
        if (coord_fd != -1) {
                close(coord_fd);
        }
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
