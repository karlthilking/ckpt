#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <ptrauth.h>

ucontext_t uc;

char *regs[33] =
{
        "x0", "x1", "x2", "x3", "x4", "x5", "x6","x7","x8","x9",
        "x10","x11", "x12", "x13", "x14", "x15", "x16", "x17",
        "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25",
        "x26", "x27", "x28", "fp", "lr", "sp", "pc"
};

void print_ucontext(ucontext_t *uc)
{
        mcontext_t mc = uc->uc_mcontext;

        for (int i = 0; i < 29; i++)
                printf("%s:\t%llu\n", regs[i], mc->__ss.__x[i]);
        
        printf("%s:\t%llu\n", regs[29], mc->__ss.__fp);
        printf("%s:\t%llu\n", regs[30], mc->__ss.__lr);
        printf("%s:\t%llu\n", regs[31], mc->__ss.__sp);
        printf("%s:\t%llu\n", regs[32], mc->__ss.__pc);

        // ptrauth_strip(IB, mc->__ss.__lr);
}

void recursive(int levels)
{
        if (levels > 0)
                recursive(levels - 1);
        else {
                getcontext(&uc);
                printf("Context after 1000 recursive calls\n");
                print_ucontext(&uc);
        }
        return;
}

int main(void)
{
        printf("Starting context\n");
        getcontext(&uc);
        print_ucontext(&uc);
        recursive(1000);
        exit(0);
}
