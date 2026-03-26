#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <time.h>
#include <pthread.h>
#include <ucontext.h>
#include <signal.h>
#include <assert.h>
#include "../include/ckpt.h"

typedef uint64_t u64;

u64             va_mask = ~((UINT64_C(1) << 48) - 1);
pthread_cond_t  cond    = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mtx     = PTHREAD_MUTEX_INITIALIZER;
int             done    = 0;

void framewalk(u64 *);

void sighandler(int sig)
{
        printf("Received signal, unsigning link return "
               "addresses on the stack...\n");

        u64 *fp;

        __asm__ volatile (
                "mov %0, fp"
                : "=r" (fp)
                :
                : "memory"
        );

        framewalk(fp);
}

void framewalk(u64 *fp)
{
        if (!fp) {
                __asm__ volatile (
                        "mov %0, fp"
                        : "=r" (fp)
                        :
                        : "memory"
                );
        }

        for (int i = 0; i < 4; i++) {
                u64 prev_fp  = *fp;
                u64 *prev_lr = fp + 1;
                u64 prev_sp  = (u64)(fp + 2);

                if (!PACSIGNED(*prev_lr, va_mask)) {
                        printf("lr:\t\t%llu\n", *prev_lr);
                } else {
                        printf("signed lr:\t%llu\n", *prev_lr);
                        XPACI(*prev_lr);
                        printf("stripped lr:\t%llu\n", *prev_lr);
                        assert(!PACSIGNED(*prev_lr, va_mask));
                        PACIB(*prev_lr, prev_sp);
                        printf("re-signed lr:\t%llu\n", *prev_lr);
                }
                
                if (prev_fp == 0 || prev_fp <= (u64)fp)
                        break;

                fp = (u64 *)prev_fp;
        }
        
        if (!done) {
                pthread_mutex_lock(&mtx);
                done = 1;
                pthread_cond_signal(&cond);
                pthread_mutex_unlock(&mtx);
        }
}

void f3()
{
        char c1, c2, c3;
        c1 = 'x' - '0'; c2 = 'x' - '0'; c3 = 'x' - '0';
        
        ucontext_t      *ucp;
        int             fd;
        
        if ((fd = open("regs", O_CREAT | O_WRONLY, S_IRWXU)) < 0)
                err(EXIT_FAILURE, "open");
        
        ucp = malloc(sizeof(ucontext_t) * 4);
        
        getcontext(ucp + 0);
        getcontext(ucp + 1);
        getcontext(ucp + 2);
        getcontext(ucp + 3);
        
        u64 fp = ucp[0].uc_mcontext->__ss.__pc;
        u64 lr = ucp[0].uc_mcontext->__ss.__lr;
        u64 sp = ucp[0].uc_mcontext->__ss.__sp;
        u64 pc = ucp[0].uc_mcontext->__ss.__sp;
        
        printf("Printing current call stack...\n");
        framewalk(NULL);
}

void f2()
{
        float f1, f2;

        f1 = 0.0f; f2 = 0.0f;
        f1 += 1.0f; f2 += 1.0f;
        
        int fd;
        
        if ((fd = open("tmp", O_CREAT | O_RDWR, S_IRWXU)) < 0)
                err(EXIT_FAILURE, "open");
        
        char buf[64];
        for (int i = 0; i < 100; i++) {
                memset(buf, 0, sizeof(buf));
                snprintf(buf, sizeof(buf), "message%d\n", i);
                write(fd, buf, strlen(buf));
        }
        
        if (lseek(fd, 0, SEEK_SET) < 0)
                err(EXIT_FAILURE, "lseek");

        if (unlink("tmp") < 0)
                err(EXIT_FAILURE, "unlink");

        f3();
}

void f1()
{
        int x1, y1, z1;
        int x2, y2, z2;
        
        x1 = y1 = z1 = x2 = y2 = z2 = 0;
        x1++; y1++; z1++; x2++; y2++; z2++;

        int     fd, rc;
        char    buf[64];
        size_t  bytes;

        if ((fd = open("/dev/null", O_WRONLY)) < 0)
                err(EXIT_FAILURE, "open");
        
        srand(time(NULL));
        for (int i = 0; i < 64; i++)
                buf[0] = (char)(rand() % ((1 << 7) - 1));
        
        char *l, *r;
        for (l = buf, r = buf + sizeof(buf); l < r; l += rc)
                if ((rc = write(fd, l, r - l)) < 0)
                        err(EXIT_FAILURE, "write");
        
        close(fd);
        f2();
}

void *worker(void *arg)
{
        signal(SIGUSR2, sighandler);
        f1();

        return NULL;
}

void *killer(void *arg)
{
        pthread_t worker_thread = *(pthread_t *)arg;

        pthread_mutex_lock(&mtx);
        while (!done)
                pthread_cond_wait(&cond, &mtx);
        
        kill(getpid(), SIGUSR2);
        pthread_mutex_unlock(&mtx);

        return NULL;
}

int main(void)
{
        pthread_t t1, t2;
        
        pthread_create(&t1, NULL, killer, (void *)&t2);
        pthread_create(&t2, NULL, worker, NULL);

        pthread_join(t1, NULL);
        pthread_join(t2, NULL);

        exit(0);
}
