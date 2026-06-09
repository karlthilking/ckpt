/* log.h */
#ifndef XND_LOG_H
#define XND_LOG_H

#include <stdio.h>

enum xnd_log_type {
        XND_ERRORS      = 0,
        XND_WARNINGS    = 1,
        XND_DEBUGGING   = 2,
        XND_TRACING     = 3
};

enum xnd_log_fd {
        XND_ERROR_FD    = 800,
        XND_WARN_FD     = 801,
        XND_DEBUG_FD    = 802,
        XND_TRACE_FD    = 803
};

#define XND_DEFAULT_LOG_LEVEL   XND_WARNINGS
#define XND_MIN_LOG_LEVEL       XND_ERRORS
#define XND_MAX_LOG_LEVEL       XND_TRACING

#define __XND_FILE__ \
        (__builtin_strrchr(__FILE__, '/') ? \
         __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#define xnd_print(__fd, __type, __fmt, ...) \
        dprintf(__fd, "[xnd:%s %s:%s]" __fmt, __type,   \
                __XND_FILE__, __func__, __VA_ARGS__)

#define xnd_error(__fmt, ...) \
        xnd_print(XND_ERROR_FD, "error", __fmt, __VA_ARGS__)
#define xnd_warn(__fmt, ...) \
        xnd_print(XND_WARN_FD, "warning", __fmt, __VA_ARGS__)
#define xnd_debug(__fmt, ...) \
        xnd_print(XND_DEBUG_FD, "debug", __fmt, __VA_ARGS__)
#define xnd_trace(__fmt, ...) \
        xnd_print(XND_TRACE_FD, "trace", __fmt, __VA_ARGS__)

#define xnd_abort() \
        do { __asm__ __volatile__("brk 777") } while (0)

#define xnd_assert(__expr) do {                                 \
        if (unlikely(!(__expr)) {                               \
                xnd_error("Assertion failed: %s\n", ##__expr);  \
                xnd_abort();                                    \
        }                                                       \
} while (0)

#define xnd_panic(__fmt, ...) do {                              \
        dprintf(STDERR_FILENO, "[xnd:panic %s:%s]" __fmt,       \
                __XND_FILE__, __func__, __VA_ARGS__);           \
        xnd_abort();                                            \
} while (0)

void xnd_log_setup(void);
void xnd_log_cleanup(void);

#endif /* XND_LOG_H */
