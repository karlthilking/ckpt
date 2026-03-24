#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/arm/thread_state.h>

void print_ucontext(ucontext_t *uc)
{
        printf("Register context from getcontext:\n");

        mcontext_t mc = uc->uc_mcontext;

        for (int i = 0; i < 29; i++)
                printf("x%d:\t%llu\n", i, mc->__ss.__x[i]);
        
        printf("fp:\t%llu\n", mc->__ss.__fp);
        printf("lr:\t%llu\n", mc->__ss.__lr);
        printf("sp:\t%llu\n", mc->__ss.__sp);
        printf("pc:\t%llu\n", mc->__ss.__pc);
}

void print_thread_state(arm_thread_state64_t *ts)
{
        printf("Register context from thread_get_state:\n");

        for (int i = 0; i < 29; i++)
                printf("x%d:\t%llu\n", i, ts->__x[i]);

        printf("fp:\t%llu\n", ts->__fp);
        printf("lr:\t%llu\n", ts->__lr);
        printf("sp:\t%llu\n", ts->__sp);
        printf("pc:\t%llu\n", ts->__pc);
}

int main(void)
{       
        ucontext_t              uc;
        kern_return_t           kr;
        arm_thread_state64_t    ts;
        mach_msg_type_number_t  count;
        
        if (getcontext(&uc) < 0)
                err(EXIT_FAILURE, "getcontext");

        /* print ucontext_t internals */
        print_ucontext(&uc);

        count = ARM_THREAD_STATE64_COUNT;
        kr = thread_get_state(mach_thread_self(),
                              ARM_THREAD_STATE64,
                              (thread_state_t)&ts,
                              &count);
        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "thread_get_state: %s\n",
                        mach_error_string(kr));
                exit(EXIT_FAILURE);
        }

        /* print arm_thread_state64_t internals */
        print_thread_state(&ts);
        exit(0);
}
