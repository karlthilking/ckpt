/* xnd_coord_api.h */
#ifndef XND_COORD_API_H
#define XND_COORD_API_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_COORDINATOR 1
#define XND_COORD_PATH  "xnd_coordinator_v0"
#define XND_COORD_FD    210

enum xnd_msghdr {
        XND_PROC_CONNECT_LAUNCH,
        XND_PROC_CONNECT_RESTART,
        XND_PROC_EXIT,
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

#define xnd_msghdr_string(h) \
        (((h) == XND_PROC_CONNECT_LAUNCH) ? "XND_PROC_CONNECT_LAUNCH" : \
         ((h) == XND_PROC_CONNECT_RESTART) ? "XND_PROC_CONNECT_RESTART" : \
         ((h) == XND_PROC_EXIT) ? "XND_PROC_EXIT" : \
         ((h) == XND_COMMAND) ? "XND_COMMAND" : \
         ((h) == XND_COORD_ACK) ? "XND_COORD_ACK" : \
         ((h) == XND_CLIENT_ACK) ? "XND_CLIENT_ACK" : \
         ((h) == XND_VIRT_TO_REAL) ? "XND_VIRT_TO_REAL" : \
         ((h) == XND_REAL_TO_VIRT) ? "XND_REAL_TO_VIRT" : \
         ((h) == XND_CKPT_REQUEST) ? "XND_CKPT_REQUEST" : \
         ((h) == XND_CKPT_READY) ? "XND_CKPT_READY" : \
         ((h) == XND_CKPT_START) ? "XND_CKPT_START" : \
         ((h) == XND_CKPT_DONE) ? "XND_CKPT_DONE" : \
         ((h) == XND_RESUME_AFTER_CKPT) ? "XND_RESUME_AFTER_CKPT" : \
         ((h) == XND_RESTART) ? "XND_RESTART" : \
         ((h) == XND_RESUME_AFTER_RESTART) ? "XND_RESUME_AFTER_RESTART" : "")

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
        
        uuid_t                  xnd_uuid;
        u32                     xnd_pid;
        u32                     xnd_ppid;
        u32                     xnd_pgid;
        
        u32                     num_peers;
        u32                     is_root_of_tree;

        pid_t                   real_pid;
        pid_t                   real_ppid;
        pid_t                   virt_pid;
        pid_t                   virt_ppid;
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void connect_to_coord(void);
void disconnect_from_coord(void);

pid_t recv_virt_to_real_pid(pid_t);
pid_t recv_real_to_virt_pid(pid_t);

int send_msg_to_coord(struct xnd_msg *);
int recv_msg_from_coord(struct xnd_msg *);
int check_coord_status(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_COORD_API_H */
