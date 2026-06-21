#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define _real_getid_0(type) ({                                  \
        register int64_t x0 __asm__("x0");                      \
        register int64_t x16 __asm__("x16") = SYS_get ##type;   \
        register int64_t x17 __asm__("x17");                    \
        __asm__ __volatile__(                                   \
                "svc #0x80              \n"                     \
                "cset %[carry], cs      \n"                     \
                : "=r" (x0), [carry] "=r" (x17)                 \
                : "r" (x16)                                     \
                : "cc", "memory"                                \
        );                                                      \
        if (x17) {                                              \
                extern int errno;                               \
                errno = x0;                                     \
                x0 = -1;                                        \
        }                                                       \
        (pid_t)x0;                                              \
})

#define _real_getpid()  _real_getid_0(pid)
#define _real_getppid() _real_getid_0(ppid)

int main(int argc, char *argv[])
{
        struct timespec ts = { 2, 0 };

        for (;;) {
                printf("getpid():   %d, _real_getpid():   %d\n",
                       getpid(), _real_getpid());
                printf("getppid():  %d, _real_getppid():  %d\n\n",
                       getppid(), _real_getppid());
                nanosleep(&ts, NULL);
        }

        return 0;
}
