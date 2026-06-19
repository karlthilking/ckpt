#ifndef XND_PID_H
#define XND_PID_H

#include "xnd/xnd.h"
#include <sys/types.h>
#include <sys/syscall.h>

#define _real_id_0(type) ({   \
        register s64 x0 __asm__("x0");  \
        register s64 x16 __asm__("x16") = SYS_get ##type;        \
        __asm__ __volatile__(                                   \
                "svc #0x80"                                     \
                : "=r" (x0)                                     \
                : "r" (x16)                                     \
                : "cc", "memory"                                \
        );                                                      \
        (pid_t)x0;                                              \
})

#define _real_id_1(type, pid) ({                                \
        register s64 x0 __asm__("x0") = (s64)(pid);             \
        register s64 x16 __asm__("x16") = SYS_get ##type;       \
        __asm__ __volatile__(                                   \
                "svc #0x80"                                     \
                : "+r" (x0)                                     \
                : "r" (x16)                                     \
                : "cc", "memory"                                \
        );                                                      \
        (pid_t)x0;                                              \
})

#define _real_getpid()  _real_id_0(pid)
#define _real_getppid() _real_id_0(ppid)
#define _real_getpgrp() _real_id_0(pgrp)
#define _real_getpgid() _real_id_1(pgid)

#endif
