#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <ucontext.h>

void f(ucontext_t *ucp)
{
        static int x;

        x = 0;
        getcontext(ucp);
        printf("%d\n", x);
        x++;
        if (x >= 10)
                exit(0);
}

int main(void)
{
        int A[1024];
        ucontext_t uc;
        
        srand(time(NULL));
        for (int i = 1; i < UINT64_C(1) << 10; i++) {
                *(A + i) = rand() % (INT_MAX >> 16);
                *(A + i - 1) += *(A + i);
        }

        f(&uc);
        setcontext(&uc);
        exit(0);
}
