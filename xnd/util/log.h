/* log.h */
#ifndef XND_LOG_H
#define XND_LOG_H

#define _XOPEN_SOURCE
#include "xnd/xnd.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

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

#define __XND_FILE__ \
        (__builtin_strrchr(__FILE__, '/') ? \
         __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#define xnd_print(fd, type, fmt, ...)                           \
        dprintf(fd, "[xnd:%s %s:%s]:\n " fmt "\n", type,        \
                __XND_FILE__, __func__, ##__VA_ARGS__)

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
        register s64 x0 __asm__("x0") = (s64)getpid();          \
        register s64 x1 __asm__("x1") = (s64)SIGABRT;           \
        register s64 x16 __asm__("x16") = (s64)SYS_kill;        \
        __asm__ __volatile__(                                   \
                "svc #0x80" :: "r" (x0), "r" (x1), "r" (x16)    \
        );                                                      \
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

void xnd_log_setup(void);
void xnd_log_cleanup(void);
void xnd_log_setup_direct(int);
void xnd_log_shared_cache_info(void);
void xnd_log_main_thread_info(void);

#endif /* XND_LOG_H */
