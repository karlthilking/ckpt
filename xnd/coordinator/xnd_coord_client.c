/* xnd_coord_client.c */
#include "xnd/xnd.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include "xnd/coordinator/xnd_coord_client.h"
#include <stdlib.h>
#include <string.h>

extern pid_t _real_pid;
extern pid_t _real_ppid;
extern pid_t _virt_pid;
extern pid_t _virt_ppid;

static __always_inline void write_msg_metainfo(struct xnd_msg *msg)
{
        msg->real_pid = _real_pid;
        msg->real_ppid = _real_ppid;
        msg->virt_pid = _virt_pid;
        msg->virt_ppid = _virt_ppid;
}

void register_with_coord_on_launch(void)
{
        int             err;
        struct xnd_msg  msg;

        msg.hdr = XND_PROC_CONNECT_LAUNCH;
        msg.real_pid = _real_getpid();
        msg.real_ppid = _real_getppid();

        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to register with coordinator!\n");
                xnd_abort();
        }
        
        err = recv_msg_from_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to receive coordinator response\n");
                xnd_abort();
        }
        
        xnd_assert(msg.hdr == XND_COORD_ACK && msg.ret == XND_SUCCESS);

        /**
         * Coordinator replies with virtual pid and ppid after this
         * process has connected, initialize _virt_pid and _virt_ppid
         * (before pid_table_init) runs.
         */
        _virt_pid = msg.virt_pid;
        _virt_ppid = msg.virt_ppid;

        xnd_trace("Registered with coordinator\n"
                  "_virt_pid=%d, _virt_ppid=%d\n", 
                  _virt_pid, _virt_ppid);
}

void register_with_coord_on_restart(void)
{
        int             err;
        struct xnd_msg  msg;

        msg.hdr = XND_PROC_CONNECT_RESTART;
        msg.virt_pid = _virt_pid;
        msg.virt_ppid = _virt_ppid;
        msg.real_pid = _real_getpid();
        msg.real_ppid = _real_getppid();

        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to register with coordinator!\n");
                xnd_abort();
        }

        xnd_assert(recv_msg_from_coord(&msg) == 0);
        xnd_assert(msg.hdr == XND_COORD_ACK && msg.ret == XND_SUCCESS);

#if DEBUG || DEVELOPMENT
        xnd_assert(msg.real_pid == _real_getpid());
        xnd_assert(msg.real_ppid == _real_getppid());
        xnd_assert(msg.virt_pid == _virt_pid);
        xnd_assert(msg.virt_ppid == _virt_ppid);
#endif
        xnd_trace("Registered with coordinator (post-restart)\n"
                  "virtual pid=%d, virtual ppid=%d\n"
                  "real pid=%d, real ppid=%d\n",
                  msg.virt_pid, msg.virt_ppid, msg.real_pid, msg.real_ppid);
}

void send_exit_to_coord(void)
{
        struct xnd_msg msg;

        msg.hdr = XND_PROC_EXIT;
        write_msg_metainfo(&msg);
        xnd_assert(send_msg_to_coord(&msg) == 0);
}

void wait_for_coord_msg(void)
{
        struct xnd_msg  msg;
        int             err;

retry:
        err = recv_msg_from_coord(&msg);
        if (err != 0) {
                if ((err = check_coord_status()) == 0) {
                        goto retry;
                }
                xnd_error("Coordinator socket error: %s\n", strerror(err));
                xnd_abort();
        }
        
        if (msg.hdr == XND_CHECKPOINT_REQUEST) {
                notify_coord_before_checkpoint();
        }
}

void notify_coord_before_checkpoint(void)
{
        struct xnd_msg  msg;
        int             err;

        msg.hdr = XND_READY_FOR_CHECKPOINT;
        write_msg_metainfo(&msg);

        err = send_msg_to_coord(&msg);
        if (unlikely(err != 0)) {
                xnd_error("Failed to send coordinator message\n");
                xnd_abort();
        }

        enter_coord_barrier(XND_READY_FOR_CHECKPOINT);
}

void notify_coord_after_checkpoint(void)
{
        struct xnd_msg  msg;
        int             err;

        msg.hdr = XND_CHECKPOINT_COMPLETE;
        write_msg_metainfo(&msg);

        err = send_msg_to_coord(&msg);
        if (unlikely(err != 0)) {
                xnd_error("Failed to send coordinator message\n");
                xnd_abort();
        }

        enter_coord_barrier(XND_CHECKPOINT_COMPLETE);
}

void enter_coord_barrier(enum xnd_msghdr event)
{
        struct xnd_msg  msg;
        int             err;

        err = recv_msg_from_coord(&msg);
        if (unlikely(err != 0)) {
                xnd_error("Failed to receive message from coordinator!\n");
                xnd_abort();
        }

        if (event == XND_READY_FOR_CHECKPOINT) {
                if (msg.hdr == XND_START_CHECKPOINT) {
                        return;
                }
                xnd_error("Unrecognized coordinator reply: %d\n", msg.hdr);
                xnd_abort();
        }

        if (event == XND_CHECKPOINT_COMPLETE) {
                if (msg.hdr == XND_RESUME_AFTER_CHECKPOINT) {
                        return;
                }
                xnd_error("Unrecognized coordinator reply: %d\n", msg.hdr);
                xnd_abort();
        }
}
