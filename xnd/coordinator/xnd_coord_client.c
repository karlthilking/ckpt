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

static int coord_fd             = -1;
static int child_coord_fd       = -1;
static int request_fd           = -1;

void send_recv_coord_handshake(enum xnd_msghdr hdr)
{
        struct xnd_msg msg;
        
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
                memcpy(msg.xnd_uuid, xnd_uuid, sizeof(uuid_t));

                msg.epoch = epoch;
                msg.num_peers = num_peers;
        }
        
        xnd_assert(send_msg_to_coord(coord_fd, &msg) == 0);
        xnd_assert(recv_msg_from_coord(coord_fd, &msg) == 0);

        if (msg.hdr != XND_COORD_ACK || msg.ret != XND_SUCCESS) {
                xnd_error("Unexpected coordinator response: %s\n",
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
                memcpy(xnd_uuid, msg.xnd_uuid, sizeof(uuid_t));
        }
}

void connect_to_coord_on_launch(void)
{
        int fd;

        fd = connect_to_coord();
        coord_fd = xnd_fd_change(fd, XND_COORD_FD);
        xnd_assert(coord_fd == XND_COORD_FD);
        
        send_recv_coord_handshake(XND_CONNECT_LAUNCH);
        xnd_trace("Registered with coordinator (on initial launch)\n"
                  "_virt_pid=%d, _virt_ppid=%d\n"
                  "_real_pid=%d, _real_ppid=%d\n"
                  "xnd_pid=%u, xnd_ppid=%u, xnd_pgid=%u\n",
                  _virt_pid, _virt_ppid, _real_getpid(), _real_getppid(),
                  xnd_pid, xnd_ppid, xnd_pgid);
}

void connect_to_coord_on_restart(void)
{
        int fd;

        fd = connect_to_coord();
        coord_fd = xnd_fd_change(fd, XND_COORD_FD);
        xnd_assert(coord_fd == XND_COORD_FD);
        
        send_recv_coord_handshake(XND_CONNECT_RESTART);
        xnd_trace("Registered with coordinator (post-restart)\n"
                  "_virt_pid=%d, _virt_ppid=%d\n"
                  "_real_pid=%d, _real_ppid=%d\n"
                  "xnd_pid=%u, xnd_ppid=%u, xnd_pgid=%u\n",
                  _virt_pid, _virt_ppid, _real_getpid(), _real_getppid(),
                  xnd_pid, xnd_ppid, xnd_pgid);
}

void disconnect_from_coord(void)
{
        struct xnd_msg msg;

        msg.hdr = XND_EXIT;
        msg.xnd_pid = xnd_pid;

        if (send_msg_to_coord(coord_fd, &msg) != 0) {
                xnd_warn("Failed to set XND_EXIT to coordinator\n");
        }

        close(coord_fd);
        if (request_fd != -1) {
                close(request_fd);
        }
}

void coord_client_atfork_prepare(void)
{
        struct xnd_msg msg;
        
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

        child_coord_fd = connect_to_coord();
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
        env_set_pid_info(msg.virt_pid, -1, -1, -1);
}

void coord_client_atfork_child(void)
{
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
                  _virt_pid, _virt_ppid, _real_pid, _real_ppid,
                  xnd_pid, xnd_ppid, xnd_pgid);
}

void coord_client_atfork_parent(void)
{
        close(child_coord_fd);
}

void coord_client_atfork_failed(void)
{
        close(child_coord_fd);
}

int wait_for_ckpt_request_from_coord(void)
{
        struct xnd_msg  msg;
        int             err;
        
        xnd_assert(coord_fd != -1);
        bzero(&msg, sizeof(msg));

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
        
        xnd_assert(msg.hdr == XND_CKPT_REQUEST);
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

        if (request_fd == -1) {
                request_fd = connect_to_coord();
                msg.xnd_pid = xnd_pid;
        }

        if (send_msg_to_coord(request_fd, &msg) != 0) {
                xnd_error("XND_VIRT_TO_REAL request failed\n");
                return -1;
        }

        if (recv_msg_from_coord(request_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                return -1;
        }

        if (msg.hdr != XND_COORD_ACK) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                return -1;
        }

        return msg.real_pid;
}

pid_t real_to_virt_pid_from_coord(pid_t real)
{
        struct xnd_msg msg;
        
        msg.hdr = XND_REAL_TO_VIRT;
        msg.real_pid = real;

        if (request_fd == -1) {
                request_fd = connect_to_coord();
                msg.xnd_pid = xnd_pid;
        }
        
        if (send_msg_to_coord(request_fd, &msg) != 0) {
                xnd_error("XND_REAL_TO_VIRT request failed\n");
                return -1;
        }
        
        if (recv_msg_from_coord(request_fd, &msg) != 0) {
                xnd_error("Failed to receive coordinator message\n");
                return -1;
        }

        if (msg.hdr != XND_COORD_ACK) {
                xnd_error("Unexpected coordinator response: %s\n",
                          xnd_msghdr_string(msg.hdr));
                return -1;
        }

        return msg.virt_pid;
}
