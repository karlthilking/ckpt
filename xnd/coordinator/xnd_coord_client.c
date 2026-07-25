/* xnd_coord_client.c */
#include "xnd/xnd.h"
#include "xnd/util/fd.h"
#include "xnd/util/env.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include "xnd_coord_api.h"
#include "xnd_coord_client.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <uuid/uuid.h>

extern pid_t    _virt_pid;
extern pid_t    _virt_ppid;
extern pid_t    _real_pid;
extern pid_t    _real_ppid;

extern uuid_t   xnd_uuid;
extern u32      xnd_pid;
extern u32      xnd_ppid;
extern u32      xnd_pgid;

extern u64      epoch;
extern u32      num_peers;
extern bool     is_root_of_tree;

static int              coord_fd        = -1;
static int              child_coord_fd  = -1;
static int              oob_fd          = -1;
static pthread_mutex_t  oob_mutex       = PTHREAD_MUTEX_INITIALIZER;

void send_recv_coord_handshake(enum xnd_msghdr hdr)
{
        struct xnd_msg msg = {0};

        msg.hdr = hdr;
        msg.real_pid = _real_getpid();
        msg.real_ppid = _real_getppid();

        if (hdr == XND_CONNECT_RESTART) {
                /**
                 * On restart, virtual pids and xnd info is already known
                 * to the process. Instead of receiving this information
                 * from the coordinator, the restarting process should
                 * send this info to the coordinator.
                 */
                msg.virt_pid = _virt_pid;
                msg.virt_ppid = _virt_ppid;

                msg.xnd_pid = xnd_pid;
                msg.xnd_ppid = xnd_ppid;
                msg.xnd_pgid = xnd_pgid;
                uuid_copy(msg.xnd_uuid, xnd_uuid);

                msg.ckpt_interval = env_get_ckpt_interval();
                msg.epoch = epoch;
                msg.num_peers = num_peers;
        }

        if (send_msg_to_coord(coord_fd, &msg) != 0) {
                xnd_error("Failed to send handshake message\n");
                xnd_abort();
        }

        if (recv_msg_from_coord(coord_fd, &msg) != 0) {
                xnd_error("Failed to receive handshake message\n");
                xnd_abort();
        } else if (msg.hdr != XND_COORD_ACK || msg.ret != XND_SUCCESS) {
                xnd_error("Handshake failed (coordinator response: %s)\n",
                          xnd_msghdr_string(msg.hdr));
                xnd_abort();
        }

        if (hdr == XND_CONNECT_LAUNCH || hdr == XND_ATFORK_CHILD) {
                /**
                 * If registering with coordinator during initial startup
                 * or after fork, the coordinator will inform this process
                 * of virtual pid info and xnd identifier info.
                 */
                _virt_pid = msg.virt_pid;
                _virt_ppid = msg.virt_ppid;

                xnd_pid = msg.xnd_pid;
                xnd_ppid = msg.xnd_ppid;
                xnd_pgid = msg.xnd_pgid;
                uuid_copy(xnd_uuid, msg.xnd_uuid);
        }
}

void connect_to_coord_on_launch(void)
{
        int fd;

        if ((fd = connect_to_coord()) < 0) {
                xnd_error("Failed to connect to coordinator\n");
                xnd_abort();
        }

        coord_fd = xnd_fd_change(fd, XND_COORD_FD);
        xnd_assert(coord_fd == XND_COORD_FD);

        send_recv_coord_handshake(XND_CONNECT_LAUNCH);
#if DEVELOPMENT || DEBUG
        xnd_trace("Registered with coordinator (on initial launch)\n"
                  "_virt_pid=%d, _virt_ppid=%d\n"
                  "_real_pid=%d, _real_ppid=%d\n"
                  "xnd_pid=%u, xnd_ppid=%u, xnd_pgid=%u\n",
                  _virt_pid, _virt_ppid, _real_getpid(), _real_getppid(),
                  xnd_pid, xnd_ppid, xnd_pgid);
#endif
}

void connect_to_coord_on_restart(void)
{
        int fd;

        if ((fd = connect_to_coord()) < 0) {
                xnd_error("Failed to connect to coordinator\n");
                xnd_abort();
        }

        coord_fd = xnd_fd_change(fd, XND_COORD_FD);
        xnd_assert(coord_fd == XND_COORD_FD);

        send_recv_coord_handshake(XND_CONNECT_RESTART);
#if DEVELOPMENT || DEBUG
        xnd_trace("Registered with coordinator (post-restart)\n"
                  "_virt_pid=%d, _virt_ppid=%d\n"
                  "_real_pid=%d, _real_ppid=%d\n"
                  "xnd_pid=%u, xnd_ppid=%u, xnd_pgid=%u\n",
                  _virt_pid, _virt_ppid, _real_getpid(), _real_getppid(),
                  xnd_pid, xnd_ppid, xnd_pgid);
#endif

        /**
         * Set oob_fd = -1 so the next thread to use this file
         * descriptor will know to reinitialize it.
         */
        oob_fd = -1;
}

void notify_coord_of_exit(pid_t pid)
{
        struct xnd_msg msg = {0};

        msg.hdr = XND_EXIT;
        msg.real_pid = pid;

        /**
         * Use out-of-band communication channel to send exit message to
         * coordinator, as any user thread may need to notify the
         * coordinator in the event of an exit (e.g., a user thread that
         * was in __waitpid_hook).
         */
        pthread_mutex_lock(&oob_mutex);
        if (oob_fd == -1) {
                if ((oob_fd = connect_to_coord()) < 0) {
                        xnd_error("Failed to connect to coordinator\n");
                        pthread_mutex_unlock(&oob_mutex);
                        return;
                }
                msg.xnd_pid = xnd_pid;
        }

        if (send_msg_to_coord(oob_fd, &msg) != 0)
                xnd_error("Failed to send XND_EXIT to coordinator\n");

        pthread_mutex_unlock(&oob_mutex);
}

void disconnect_from_coord(void)
{
        struct xnd_msg msg;

        msg.hdr = XND_EXIT;
        msg.real_pid = _real_pid;
        xnd_assert(_real_pid == _real_getpid());

        if (send_msg_to_coord(coord_fd, &msg) != 0) {
                xnd_warn("Failed to send XND_EXIT to coordinator\n");
        }

        if (close(coord_fd) != 0) {
                xnd_warn("close: %s\n", strerror(errno));
        }

        if (oob_fd != -1 && close(oob_fd) != 0) {
                xnd_warn("close: %s\n", strerror(errno));
        }
}

void coord_client_atfork_prepare(void)
{
        int             err;
        struct xnd_msg  msg;

        /* Acquire coordinator out-of-band communication mutex */
        if ((err = pthread_mutex_lock(&oob_mutex)) != 0) {
                xnd_error("pthread_mutex_lock: %s\n", strerror(err));
                xnd_abort();
        }

        /**
         * Connect to coordinator for child before fork and send
         * XND_ATFORK_PREPARE to the coordinator.
         *
         * The coordinator should pre-allocate a virtual pid for the
         * child and send it to the parent (here), and then wait for
         * the child to send a handshake (in coord_client_atfork_child).
         */
        msg.hdr = XND_ATFORK_PREPARE;
        msg.virt_ppid = _virt_pid;
        msg.xnd_ppid = xnd_pid;
        msg.xnd_pgid = xnd_pgid;

        if ((child_coord_fd = connect_to_coord()) < 0) {
                xnd_error("Failed to connect to coordinator for child\n");
                xnd_abort();
        }

        if (send_msg_to_coord(child_coord_fd, &msg) != 0) {
                xnd_error("Failed to send XND_ATFORK_PREPARE");
                xnd_abort();
        }

        if (recv_msg_from_coord(child_coord_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                xnd_abort();
        }

        if (msg.hdr != XND_COORD_ACK || msg.ret != XND_SUCCESS) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                xnd_abort();
        }

        /**
         * Set environment variable "XND_VIRTUAL_PID"=msg.virt_pid
         * so the child's virtual pid (selected by the coordinator) can
         * be discovered in __fork_wrapper()
         *
         * Then, in __fork_wrapper(), the parent will be able to update the
         * virtual pid table with pid_table_update(child_virt, child_real)
         * and return child_virt from __fork_wrapper() so the caller of
         * fork() correctly sees the child's virtual pid.
         */
        xnd_trace("Child virtual pid: %d\n", msg.virt_pid);
        env_set_pid_info(msg.virt_pid, -1, -1, -1);
}

void coord_client_atfork_child(void)
{
        int err;

        if ((err = pthread_mutex_unlock(&oob_mutex)) != 0) {
                xnd_error("pthread_mutex_unlock: %s\n", strerror(err));
                xnd_abort();
        }

        if ((err = pthread_mutex_init(&oob_mutex, NULL)) != 0) {
                xnd_error("pthread_mutex_init: %s\n", strerror(err));
                xnd_abort();
        }

        coord_fd = xnd_fd_change(child_coord_fd, XND_COORD_FD);
        xnd_assert(coord_fd == XND_COORD_FD);

        /**
         * Coordinator will send virtual pid info and xnd pid info
         * via handshake exchange with child
         */
        send_recv_coord_handshake(XND_ATFORK_CHILD);
        xnd_trace("Registered with coordinator (after fork):\n"
                  "_virt_pid=%d, _virt_ppid=%d\n"
                  "_real_pid=%d, _real_ppid=%d\n"
                  "xnd_pid=%u, xnd_ppid=%u, xnd_pgid=%u\n",
                  _virt_pid, _virt_ppid, _real_getpid(), _real_getppid(),
                  xnd_pid, xnd_ppid, xnd_pgid);

        /**
         * If oob_fd is inherited from parent, close and set to -1
         * so it will be reinitialized properly. oob_fd can not be
         * used by the child process until the coordinator is notified
         * about which process is sending the request; the coordinator
         * needs to be able to associate the connection with one of
         * the processes that it knows about.
         */
        if (oob_fd != -1) {
                close(oob_fd);
                oob_fd = -1;
        }
}

void coord_client_atfork_parent(void)
{
        int err;

        if ((err = pthread_mutex_unlock(&oob_mutex)) != 0) {
                xnd_error("pthread_mutex_unlock: %s\n", strerror(err));
                xnd_abort();
        }

        if (close(child_coord_fd) != 0) {
                xnd_warn("close: %s\n", strerror(errno));
        }
}

void coord_client_atfork_failed(void)
{
        int err;

        if ((err = pthread_mutex_unlock(&oob_mutex)) != 0) {
                xnd_error("pthread_mutex_unlock: %s\n", strerror(err));
                xnd_abort();
        }

        if (close(child_coord_fd) != 0) {
                xnd_warn("close: %s\n", strerror(errno));
        }
}

int wait_for_ckpt_request_from_coord(void)
{
        struct xnd_msg  msg = {0};
        int             err;

        xnd_assert(coord_fd != -1);
        for (;;) {
                err = recv_msg_from_coord(coord_fd, &msg);
                if (err == 0) {
                        break;
                } else if (coord_exited(coord_fd)) {
                        return -1;
                } else if ((err = coord_socket_status(coord_fd)) != 0) {
                        xnd_error("Coordinator socket error: %s\n",
                                  strerror(err));
                        return -1;
                }
        }

        if (msg.hdr != XND_CKPT_REQUEST) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                return -1;
        }

        return 0;
}

void enter_coord_barrier(enum coord_barrier_type type)
{
        struct xnd_msg  msg;
        enum xnd_msghdr expected;

        switch (type) {
        case COORD_BARRIER_PRECKPT: {
                expected = XND_CKPT_START;
                msg.hdr = XND_CKPT_READY;
                if (send_msg_to_coord(coord_fd, &msg) != 0) {
                        xnd_error("Failed to send XND_CKPT_READY\n");
                        xnd_abort();
                }
                break;
        }
        case COORD_BARRIER_POSTCKPT: {
                expected = XND_RESUME_AFTER_CKPT;
                msg.hdr = XND_CKPT_DONE;
                if (send_msg_to_coord(coord_fd, &msg) != 0) {
                        xnd_error("Failed to send XND_CKPT_DONE\n");
                        xnd_abort();
                }
                break;
        }
        case COORD_BARRIER_POSTRESTART: {
                expected = XND_RESUME_AFTER_RESTART;
                break;
        }
        default:
                __builtin_trap();
        }

        if (recv_msg_from_coord(coord_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                xnd_abort();
        }

        if (msg.hdr != expected) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                xnd_abort();
        }

        if (type == COORD_BARRIER_PRECKPT) {
                /**
                 * If in pre-checkpoint barrier, then coordinator sends
                 * number of peers in computation and process tree root
                 * information here.
                 */
                num_peers = msg.num_peers;
                is_root_of_tree = msg.is_root_of_tree;
        }
}

pid_t virt_to_real_pid_from_coord(pid_t virt)
{
        struct xnd_msg msg;

        msg.hdr = XND_VIRT_TO_REAL;
        msg.virt_pid = virt;

        xnd_assert(pthread_mutex_lock(&oob_mutex) == 0);
        if (oob_fd == -1) {
                if ((oob_fd = connect_to_coord()) < 0) {
                        xnd_error("Failed to connect to coordinator\n");
                        goto fail;
                }
                msg.xnd_pid = xnd_pid;
        }

        if (send_msg_to_coord(oob_fd, &msg) != 0) {
                xnd_error("XND_VIRT_TO_REAL request failed\n");
                goto fail;
        }

        if (recv_msg_from_coord(oob_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                goto fail;
        }

        if (msg.hdr != XND_COORD_ACK) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                goto fail;
        } else if (msg.ret != XND_SUCCESS) {
                xnd_error("Coordinator could not find virtual -> real "
                          "mapping for virtual pid %d\n", virt);
                goto fail;
        }

        xnd_assert(pthread_mutex_unlock(&oob_mutex) == 0);
        return msg.real_pid;
fail:
        xnd_assert(pthread_mutex_unlock(&oob_mutex) == 0);
        return -1;
}

pid_t real_to_virt_pid_from_coord(pid_t real)
{
        struct xnd_msg msg;

        msg.hdr = XND_REAL_TO_VIRT;
        msg.real_pid = real;

        xnd_assert(pthread_mutex_lock(&oob_mutex) == 0);
        if (oob_fd == -1) {
                if ((oob_fd = connect_to_coord()) < 0) {
                        xnd_error("Failed to connect to coordinator\n");
                        goto fail;
                }
                msg.xnd_pid = xnd_pid;
        }

        if (send_msg_to_coord(oob_fd, &msg) != 0) {
                xnd_error("XND_REAL_TO_VIRT request failed\n");
                goto fail;
        }

        if (recv_msg_from_coord(oob_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                goto fail;
        }

        if (msg.hdr != XND_COORD_ACK) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                goto fail;
        } else if (msg.ret != XND_SUCCESS) {
                xnd_error("Coordinator could not find real -> virtual "
                          "mapping for real pid %d\n", real);
                goto fail;
        }

        xnd_assert(pthread_mutex_unlock(&oob_mutex) == 0);
        return msg.virt_pid;
fail:
        xnd_assert(pthread_mutex_unlock(&oob_mutex) == 0);
        return -1;
}
