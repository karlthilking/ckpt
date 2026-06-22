/* xnd_coord_api.h */
#ifndef XND_COORD_API_H
#define XND_COORD_API_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_COORDINATOR 1
#define XND_COORD_PATH  "xnd_coordinator_v0"

enum xnd_msghdr {
        XND_PROC_CONNECT_LAUNCH,
        XND_PROC_CONNECT_RESTART,
        XND_PROC_EXIT,
        XND_COMMAND,
        XND_COORD_ACK,
        XND_CLIENT_ACK
};

enum xnd_command {
        XND_NULL_CMD,
        XND_CKPT_CMD,
        XND_EXIT_CMD,
        XND_KILL_CMD
};

enum xnd_coord_return {
        XND_SUCCESS,
        XND_FAILURE 
};

struct xnd_msg {
        enum xnd_msghdr         hdr;
        enum xnd_command        cmd;
        enum xnd_coord_return   ret;

        pid_t                   real_pid;
        pid_t                   real_ppid;
        pid_t                   virt_pid;
        pid_t                   virt_ppid;
};

void connect_to_coord(void);
void disconnect_from_coord(void);

int send_msg_to_coord(struct xnd_msg *);
int recv_msg_from_coord(struct xnd_msg *);

#endif /* XND_COORD_API_H */
