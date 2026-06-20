/* coord_common.h */
#ifndef XND_COORD_COMMON_H
#define XND_COORD_COMMON_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_COORD_PATH "xnd_coord_v0"

enum xnd_msghdr {
        XND_PROC_EXIT,
        XND_VIRT_PID_INFO,
        XND_REAL_PID_INFO,
        XND_ATFORK_PREPARE,
        XND_ATFORK_PARENT,
        XND_ATFORK_CHILD,
        XND_ATFORK_FAILED,
        XND_CHECKPOINT,
        XND_RESUME,
};

struct pid_info {
        pid_t   pid;
        pid_t   ppid;
};

#endif /* XND_COORD_COMMON_H */
