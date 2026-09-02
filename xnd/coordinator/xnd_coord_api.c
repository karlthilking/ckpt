/* xnd_coord_api.c */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "common/time.h"
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/pid/pid.h"
#include "xnd/util/io.h"
#include "xnd/util/path.h"
#include "xnd/platform/exe.h"
#include "xnd/pid/pid_table.h"
#include "xnd_coord_api.h"
#include "xnd_coord_common.h"

pid_t launch_coordinator(bool restarting)
{
        char    buf[PATH_MAX], exe[PATH_MAX];
        pid_t   coord_pid;

        if (access(XND_COORD_PATH, F_OK) == 0)
                xnd_assert(unlink(XND_COORD_PATH) == 0);

        coord_pid = fork();
        switch (coord_pid) {
        case -1: {
                xnd_error("fork: %s\n", strerror(errno));
                return -1;
        }
        case 0: {
                xnd_exe_dir(buf, sizeof(buf));
                xnd_path_join(exe, PATH_MAX, buf, "xnd_coordinator");

                char *argv[] = { exe, NULL, NULL };
                if (restarting)
                        argv[1] = XND_COORD_RESTART_FLAG;

                if (execvp(argv[0], argv) != 0) {
                        xnd_perror("execvp");
                        exit(XND_EXIT_FAILURE);
                }

                unreachable();
        }
        default:
                break;
        }

        snprintf(buf, sizeof(buf), "%d", coord_pid);
        xnd_assert(setenv("XND_COORD_PID", buf, 1) == 0);

        return coord_pid;
}

pid_t
get_coord_pid(void)
{
	static pid_t coord_pid = -1;
	char *pid_str = NULL;

	if (coord_pid == -1) {
		pid_str = getenv("XND_COORD_PID");
		if (pid_str != NULL)
			coord_pid = atoi(pid_str);
	}

	return coord_pid;
}

int connect_to_coord(void)
{
	bool retry;
	int fd, ret, tries = 0;
	struct sockaddr_un addr;
	struct timespec ts = {0};

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		xnd_perror("socket");
		return -1;
	}

	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path));

	do {
		ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
		if (ret != 0) {
			retry = (errno == ENOENT || errno == ECONNREFUSED);
			if (retry && tries++ < 100) {
				ts.tv_sec = 0;
				ts.tv_nsec = 2 * NSEC_PER_MSEC;
				nanosleep(&ts, NULL);
			} else {
				xnd_perror("connect");
				goto out;
			}
		}
	} while (ret != 0);

	ret = fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (ret != 0) {
		xnd_perror("fcntl");
		goto out;
	}

out:
	if (ret) {
		close(fd);
		return -1;
	}

	return fd;
}

int
send_command_to_coord(struct xnd_msg *msg, int timeout, bool *exited)
{
	int fd, ret;
	bool check_exit = true;
	struct xnd_msg resp = {0};
	struct timeval tv = { .tv_sec = timeout, .tv_usec = 0 };

	if (exited != NULL)
		*exited = false;

	fd = connect_to_coord();
	if (fd < 0) {
		xnd_error("failed to connect to coordinator\n");
		return -1;
	}

	if (timeout != 0) {
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	}

	ret = send_msg_to_coord(fd, msg);
	if (ret != 0)
		goto out;

	ret = recv_msg_from_coord(fd, &resp);
	if (ret != 0)
		goto out;

	check_exit = false;
	if (resp.hdr != XND_COORD_ACK || resp.ret != XND_SUCCESS)
		ret = -1;

out:
	if (check_exit && ret != 0 && exited != NULL)
		*exited = (!coord_socket_exists() || peer_exited(fd));

        close(fd);
	return ret;
}

int
send_msg_to_coord(int fd, struct xnd_msg *msg)
{
        ssize_t bytes;

	if (fd == -1)
		return -1;

	bytes = writeall(fd, msg, sizeof(*msg));
	if (bytes != sizeof(*msg))
		return -1;

        return 0;
}

int
recv_msg_from_coord(int fd, struct xnd_msg *msg)
{
	ssize_t bytes;

	if (fd == -1)
		return -1;

	bytes = readall(fd, msg, sizeof(*msg));
	if (bytes != sizeof(*msg))
		return -1;

	return 0;
}

bool
peer_exited(int fd)
{
	char buf[1];
	int flags;
	ssize_t ret;
	bool blocking, exited;

	flags = fcntl(fd, F_GETFL);
	blocking = (0 == (flags & O_NONBLOCK));
	if (blocking)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	ret = recv(fd, buf, sizeof(buf), MSG_PEEK);
	exited = ((ret == 0) || (ret < 0 && errno == ECONNRESET));

	if (blocking)
		fcntl(fd, F_SETFL, flags);

	return exited;
}

bool
coord_socket_exists(void)
{
	return (access(XND_COORD_PATH, F_OK) == 0);
}
