#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <ucontext.h>
#include <ptrauth.h>

typedef uint64_t u64;

/* Strip PAC from pointer to instruction address */
#define PACSTRIP_I(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpaci %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Strip PAC from pointer to data address */
#define PACSTRIP_D(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpacd %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with IA key using a modifier */
#define PACSIGN_IA(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacia %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with IB key using a modifier */
#define PACSIGN_IB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

#define PACSIGNED(ptr) ((u64)(ptr) > (((u64)(1) << 48) - 1))

int main(void)
{
        ucontext_t uc;
        getcontext(&uc);
        mcontext_t mc = uc.uc_mcontext;
        
        printf("original lr:\t%llu\n", mc->__ss.__lr);
        assert(PACSIGNED(mc->__ss.__lr));
        
        PACSTRIP_I(mc->__ss.__lr);
        printf("stripped lr:\t%llu\n", mc->__ss.__lr);
        assert(!PACSIGNED(mc->__ss.__lr));
        
        PACSIGN_IB(mc->__ss.__lr, mc->__ss.__sp);
        printf("re-signed lr:\t%llu\n", mc->__ss.__lr);
        assert(PACSIGNED(mc->__ss.__lr));

        for (int i = 0; i < 29; i++) {
                if (PACSIGNED(mc->__ss.__x[i])) {
                        printf("x%d:\t%llu\n",
                                i, mc->__ss.__x[i]);
                }
        }

        if (PACSIGNED(mc->__ss.__sp))
                printf("signed sp:\t%llu\n", mc->__ss.__sp);
        if (PACSIGNED(mc->__ss.__fp))
                printf("signed fp:\t%llu\n", mc->__ss.__fp);
        if (PACSIGNED(mc->__ss.__pc))
                printf("signed pc:\t%llu\n", mc->__ss.__pc);
        
        exit(0);
}
