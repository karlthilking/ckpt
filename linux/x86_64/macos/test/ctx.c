#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

char *regs[28] =
{
        "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
        "x8", "x9", "x15", "x16", "x17", "x18", "x19",
        "x20", "x21", "x22", "x23", "x24", "x25", "x26",
        "x27", "x28", "fp", "lr", "sp", "pc"
};

/**
 * 
 *
 *
 *
 *
 *
 *
 */
int main(void)
{
        srand(time(NULL));
        
        int A[1024];
        ucontext_t uc;
        
        for (int i = 1; i < 1024; i++) {
                *(A + i) = rand() % 1024;
                *(A + i - 1) = *(A + i) * 2 + 3;
        }
        
        static int count;
        count = 0;
        getcontext(&uc);
        printf("%d\n", count++);
        
        // for (int i = 0; i < 31; i++)
        //         printf("%llu\n", uc.uc_mcontext->gregs[i]);
        // printf("%llu\n", uc.uc_mcontext->sp);
        // printf("%llu\n", uc.uc_mcontext->pc);

        if (count == 1)
                setcontext(&uc);
        exit(0);
}
