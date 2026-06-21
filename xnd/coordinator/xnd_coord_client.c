/* xnd_coord_client.c */
#include "xnd/xnd.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include <stdlib.h>
#include <string.h>

void register_with_coord_on_launch(void)
{
        int             err;
        char            buf[32];
        struct xnd_msg  msg;

        msg.hdr = XND_PROC_CONNECT_LAUNCH;
        msg.real_pid = _real_getpid();
        msg.real_ppid = _real_getppid();

        err = send_msg_to_coord(&msg);
        if (err != 0) {
                xnd_error("Failed to register with coordinator!\n");
                xnd_abort();
        }

        xnd_assert(recv_msg_from_coord(&msg) == 0);
        xnd_assert(msg.hdr == XND_COORD_ACK && msg.ret == XND_SUCCESS);

        /**
         * Coordinator will reply with virtual pid and ppid selected for
         * this process.
         * Then, set XND_VIRT_PID=virt_pid, XND_VIRT_PPID=virt_ppid so
         * pid_table_init will receive the correct virtual pid and ppid.
         */
        bzero(buf, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", msg.virt_pid);
        xnd_assert(setenv("XND_VIRT_PID", buf, 1) == 0);

        bzero(buf, sizeof(buf));
        snprintf(buf, sizeof(buf), "%d", msg.virt_ppid);
        xnd_assert(setenv("XND_VIRT_PPID", buf, 1) == 0);

        xnd_trace("Registered with coordinator\n"
                  "XND_VIRT_PID=%d, XND_VIRT_PPID=%d\n",
                  msg.virt_pid, msg.virt_ppid);
}

void register_with_coord_on_restart(void)
{
        int             err;
        struct xnd_msg  msg;

        msg.hdr = XND_PROC_CONNECT_RESTART;
        msg.virt_pid = pid_table_getpid();
        msg.virt_ppid = pid_table_getppid();
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
        xnd_assert(msg.virt_pid == pid_table_getpid());
        xnd_assert(msg.virt_ppid = pid_table_getppid());
#endif
        xnd_trace("Register with coordinator (post-restart)\n"
                  "virtual pid=%d, virtual ppid=%d\n"
                  "real pid=%d, real ppid=%d\n",
                  msg.virt_pid, msg.virt_ppid, msg.real_pid, msg.real_ppid);
}

void send_exit_to_coord(void)
{
        struct xnd_msg msg;

        msg.hdr = XND_PROC_EXIT;
        xnd_assert(send_msg_to_coord(&msg) == 0);
}
