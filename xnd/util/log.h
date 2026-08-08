/* log.h */
#ifndef XND_LOG_H
#define XND_LOG_H

#include "xnd/xnd.h"
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

struct thread_info;

enum xnd_log_level {
        XND_ERRORS      = 0,
        XND_WARNINGS    = 1,
        XND_DEBUGGING   = 2,
        XND_TRACING     = 3
};

#define XND_TRACE_FD 253

#define XND_MIN_LOG_LEVEL       XND_ERRORS
#define XND_MAX_LOG_LEVEL       XND_TRACING

#if DEVELOPMENT || DEBUG
# define XND_DEFAULT_LOG_LEVEL XND_TRACING
#else
# define XND_DEFAULT_LOG_LEVEL XND_WARNINGS
#endif

#ifndef _real_getpid
# define _real_getpid()						\
({								\
        register s64 x0 __asm__("x0");                          \
        register s64 x16 __asm__("x16") = (s64)SYS_getpid;      \
        __asm__ __volatile__(                                   \
                "svc #0x80" : "=r" (x0) : "r" (x16)             \
        );                                                      \
        (pid_t)x0;                                              \
})
#endif

#define __XND_FILE__				\
	(__builtin_strrchr(__FILE__, '/')	\
	 ? __builtin_strrchr(__FILE__, '/') + 1 \
	 : __FILE__)

#define CLR_RED "\033[0;31m"
#define CLR_YLW "\033[0;33m"
#define CLR_RST "\033[0m"

#define xnd_printf(fmt, ...) \
	printf("[xnd] " fmt, ##__VA_ARGS__)

#define xnd_perror(s) \
	fprintf(stderr, CLR_RED "[xnd:%s:%s:%d] " s ": %s\n" CLR_RST, \
		__XND_FILE__, __func__, __LINE__, strerror(errno))

#define xnd_error(fmt, ...) \
	fprintf(stderr, CLR_RED "[xnd:%s:%s:%d] " fmt CLR_RST, \
		__XND_FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define xnd_warn(fmt, ...) \
	fprintf(stderr, CLR_YLW "[xnd:%s:%s:%d] " fmt CLR_RST, \
		__XND_FILE__, __func__, __LINE__, ##__VA_ARGS__)

/**
 * Print information to xnd.log file instead of stdout/stderr
 */
#define xnd_trace(fmt, ...) \
	dprintf(XND_TRACE_FD, "[xnd:%s:%s:%d] " fmt, \
		__XND_FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define xnd_abort()						     \
	do {							     \
		register s64 x0 __asm__("x0") = (s64)_real_getpid(); \
		register s64 x1 __asm__("x1") = (s64)SIGABRT;	     \
		register s64 x16 __asm__("x16") = (s64)SYS_kill;     \
	        __asm__ __volatile__ (				     \
			"svc #0x80" :: "r" (x0), "r" (x1), "r" (x16) \
			);					     \
		unreachable();					     \
	} while (0)

#define xnd_panic(fmt, ...)		       \
	do {				       \
		xnd_error(fmt, ##__VA_ARGS__); \
		xnd_abort();		       \
	} while (0)

#if DEBUG || DEVELOPMENT
# define xnd_assert(expr)					     \
	do {							     \
		if (unlikely(!(expr))) {			     \
			xnd_error("assertion failure: %s\n", #expr); \
			xnd_abort();				     \
		}						     \
	} while (0)
#else /* !(DEBUG || DEVELOPMENT) */
# define xnd_assert(expr) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void xnd_log_setup(void);
void xnd_log_cleanup(void);

void xnd_log_shared_cache_info(void);
void xnd_log_ckpt_thread_info(struct thread_info *);
void xnd_log_main_thread_info(void);
void xnd_log_mach_port_info(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_LOG_H */
