/* syscall.h */
#ifndef XND_SYSCALL_H
#define XND_SYSCALL_H
#include "xnd/xnd.h"
#include <errno.h>
#include <sys/syscall.h>

static __always_inline ssize_t sys_read(int fd, void *buf, size_t nbyte)
{
        register s64 x0 __asm__("x0") = (s64)fd;
        register u64 x1 __asm__("x1") = (u64)buf;
        register u64 x2 __asm__("x2") = (u64)nbyte;
        register s64 x16 __asm__("x16") = (s64)SYS_read;
        register u64 x17 __asm__("x17");

        asm volatile(
                "svc #0x80		\n"
                "cset %[carry], cs	\n"
                : "+r" (x0), [carry] "=r" (x17)
                : "r" (x1), "r" (x2), "r" (x16)
                : "cc", "memory"
        );

	if (x17 != 0) {
		errno = x0;
		x0 = -1;
	}

	return (ssize_t)x0;
}

static __always_inline ssize_t sys_write(int fd, const void *buf,
                                         size_t nbyte)
{
        register s64 x0 __asm__("x0") = (s64)fd;
        register u64 x1 __asm__("x1") = (u64)buf;
        register u64 x2 __asm__("x2") = (u64)nbyte;
        register s64 x16 __asm__("x16") = (s64)SYS_write;
        register u64 x17 __asm__("x17");

        asm volatile(
                "svc #0x80		\n"
                "cset %[carry], cs	\n"
                : "+r" (x0), [carry] "=r" (x17)
                : "r" (x1), "r" (x2), "r" (x16)
                : "cc", "memory"
        );

	if (x17 != 0) {
		errno = x0;
		x0 = -1;
	}

	return (ssize_t)x0;
}

static __always_inline ssize_t sys_readall(int fd, void *buf,
                                           size_t nbyte)
{
	ssize_t ret;
	size_t bytes = 0;

	do {
		ret = sys_read(fd, buf + bytes, nbyte - bytes);
	} while ((ret > 0) && ((bytes += ret) < nbyte));

        return bytes;
}

static __always_inline ssize_t sys_writeall(int fd, const void *buf,
					    size_t nbyte)
{
	ssize_t ret;
	size_t bytes = 0;

	do {
		ret = sys_write(fd, buf + bytes, nbyte - bytes);
	} while ((ret != -1) && ((bytes += ret) < nbyte));

	return bytes;
}

#endif /* XND_SYSCALL_H */
