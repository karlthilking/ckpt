/* xnd_coord_api.h */
#ifndef XND_COORD_API_H
#define XND_COORD_API_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_COORDINATOR         1
#define XND_COORD_PATH          "xnd_coordinator_v0"
#define XND_COORD_RESTART_FLAG  "xnd-coord-restart"
#define XND_COORD_FD            210

#define OOB_FD_NULL -2
#define OOB_FD_FREE -3

#define XND_MSGHDR_MIN XND_CONNECT_LAUNCH
#define XND_MSGHDR_MAX XND_RESUME_AFTER_RESTART
enum xnd_msghdr {
        XND_CONNECT_LAUNCH,
        XND_CONNECT_RESTART,
        XND_EXIT,
        XND_ATFORK_PREPARE,
        XND_ATFORK_CHILD,
        XND_ATFORK_FAILED,
        XND_COMMAND,
        XND_COORD_ACK,
        XND_CLIENT_ACK,
        XND_VIRT_TO_REAL,
        XND_REAL_TO_VIRT,
        XND_CKPT_REQUEST,
        XND_CKPT_READY,
        XND_CKPT_START,
        XND_CKPT_DONE,
        XND_RESUME_AFTER_CKPT,
        XND_RESTART,
        XND_RESUME_AFTER_RESTART
};

#define XND_CMD_MIN XND_NULL_CMD
#define XND_CMD_MAX XND_KILL_CMD
enum xnd_cmd {
        XND_NULL_CMD,
        XND_CKPT_CMD,
        XND_EXIT_CMD,
        XND_KILL_CMD
};

enum coord_barrier_type {
        COORD_BARRIER_PRECKPT,
        COORD_BARRIER_POSTCKPT,
        COORD_BARRIER_POSTRESTART
};

enum xnd_coord_return {
        XND_SUCCESS,
        XND_FAILURE,
};

struct xnd_msg {
        enum xnd_msghdr         hdr;
        enum xnd_cmd            cmd;
        enum xnd_coord_return   ret;

        uuid_t                  xnd_uuid;
        u32                     xnd_pid;
        u32                     xnd_ppid;
        u32                     xnd_pgid;

        u64                     epoch;
        u32                     num_peers;
        u32                     is_root_of_tree;
        int                     ckpt_interval;

        pid_t                   real_pid;
        pid_t                   real_ppid;
        pid_t                   virt_pid;
        pid_t                   virt_ppid;
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

pid_t launch_coordinator(bool);
pid_t get_coord_pid(void);

int connect_to_coord(void);
int send_command_to_coord(enum xnd_cmd, int, bool *);
int send_msg_to_coord(int, struct xnd_msg *);
int recv_msg_from_coord(int, struct xnd_msg *);

bool peer_exited(int);
bool coord_socket_exists(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_COORD_API_H */
