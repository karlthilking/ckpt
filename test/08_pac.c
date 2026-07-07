#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

static void *loop(void *);
static inline void *print_return_address(void *);
static void *print_func_ptrs(void *);
static void *sshh(void *);

#define NFUNC_PTRS 4
static struct fnctable {
        void *(*f[4])(void *);
} fnctable = {
        .f = { loop, print_return_address, print_func_ptrs, sshh }
};

static void *loop(void *arg)
{
        for (int l = 0; l < (1 << 10); ++l) {
                for (uint64_t i = 0u; i < UINT64_MAX; i = ((i << 1) | 1))
                        ;
        }
        return NULL;
}

static inline void *print_return_address(void *arg)
{
        uint64_t lr;

        asm volatile("mov %0, lr" : "=r" (lr) :: "memory");
        printf("return address:\t0x%llx\n", lr);

        return NULL;
}

static void *print_func_ptrs(void *arg)
{
        void *(*f)(void *);

        for (int i = 0; i < NFUNC_PTRS; i++) {
                f = fnctable.f[i];
                printf("function pointer #%d: %p\n", i, (void *)f);
                for (int j = 0; j < 10; j++)
                        sshh(NULL);
        }

        return NULL;
}

static void *sshh(void *arg)
{
        usleep(100000);
        return NULL;
}

static void *recursive(void *arg)
{
        long depth = (long)arg;

        if (depth > 0) {
                for (int i = 0; i < NFUNC_PTRS; i++) {
                        if (fnctable.f[i] == sshh) {
                                for (int j = 0; j < 15; j++)
                                        (fnctable.f[i])(NULL);
                        } else {
                                (fnctable.f[i])(NULL);
                        }
                }
                recursive((void *)(depth - 1));
        }

        return NULL;
}

int main(int argc, char *argv[])
{
        recursive((void *)1000L);
        exit(0);
}
