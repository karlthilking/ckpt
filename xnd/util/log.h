/* log.h */
#ifndef XND_LOG_H
#define XND_LOG_H

#define _XOPEN_SOURCE
#include "xnd/xnd.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

struct thread_info;

enum xnd_log_type {
        XND_ERRORS      = 0,
        XND_WARNINGS    = 1,
        XND_DEBUGGING   = 2,
        XND_TRACING     = 3
};

enum xnd_log_fd {
        XND_ERROR_FD    = 250,
        XND_WARN_FD     = 251,
        XND_DEBUG_FD    = 252,
        XND_TRACE_FD    = 253
};

#if DEVELOPMENT || DEBUG
# define XND_DEFAULT_LOG_LEVEL XND_TRACING
#else
# define XND_DEFAULT_LOG_LEVEL XND_WARNINGS
#endif

#define XND_MIN_LOG_LEVEL       XND_ERRORS
#define XND_MAX_LOG_LEVEL       XND_TRACING

#ifndef _real_getpid
# define _real_getpid() ({                                      \
        register s64 x0 __asm__("x0");                          \
        register s64 x16 __asm__("x16") = (s64)SYS_getpid;      \
        __asm__ __volatile__(                                   \
                "svc #0x80" : "=r" (x0) : "r" (x16)             \
        );                                                      \
        (pid_t)x0;                                              \
})
#endif

#define __XND_FILE__ \
        (__builtin_strrchr(__FILE__, '/') ? \
         __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#define xnd_print(fd, type, fmt, ...)                   \
        dprintf(fd, "[xnd:%s %s:%s %d]:\n" fmt "\n",    \
                type, __XND_FILE__, __func__,           \
                _real_getpid(), ##__VA_ARGS__)

#define xnd_printf(fmt, ...) \
        printf("[xnd]: " fmt "\n", ##__VA_ARGS__)

#define xnd_error(__fmt, ...) \
        xnd_print(XND_ERROR_FD, "error", __fmt, ##__VA_ARGS__)
#define xnd_warn(__fmt, ...) \
        xnd_print(XND_WARN_FD, "warning", __fmt, ##__VA_ARGS__)
#define xnd_debug(__fmt, ...) \
        xnd_print(XND_DEBUG_FD, "debug", __fmt, ##__VA_ARGS__)
#define xnd_trace(__fmt, ...) \
        xnd_print(XND_TRACE_FD, "trace", __fmt, ##__VA_ARGS__)

#define xnd_abort() do { \
        register s64 x0 __asm__("x0") = (s64)_real_getpid();    \
        register s64 x1 __asm__("x1") = (s64)SIGABRT;           \
        register s64 x16 __asm__("x16") = (s64)SYS_kill;        \
        __asm__ __volatile__(                                   \
                "svc #0x80" :: "r" (x0), "r" (x1), "r" (x16)    \
        );                                                      \
        __asm__ __volatile__("brk #777");                       \
} while (0)

#define xnd_assert(__expr) do {                                 \
        if (unlikely(!(__expr))) {                              \
                xnd_error("Assertion failed: %s\n", #__expr);   \
                xnd_abort();                                    \
        }                                                       \
} while (0)

#define xnd_panic(__fmt, ...) do {                              \
        dprintf(STDERR_FILENO, "[xnd:panic %s:%s]" __fmt,       \
                __XND_FILE__, __func__, ##__VA_ARGS__);         \
        xnd_abort();                                            \
} while (0)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void xnd_log_setup(void);
void xnd_log_cleanup(void);
void xnd_log_setup_direct(int);

void xnd_log_shared_cache_info(void);
void xnd_log_ckpt_thread_info(struct thread_info *);
void xnd_log_main_thread_info(void);
void xnd_log_mach_port_info(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_LOG_H */
