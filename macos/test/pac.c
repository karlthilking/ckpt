#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sys/sysctl.h>
#include <ucontext.h>
#include <ptrauth.h>

typedef uint64_t        u64;
typedef uint32_t        u32;
typedef uint16_t        u16;
typedef uint8_t         u8;

/* Strip PAC from pointer to instruction address */
#define XPACI(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpaci %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Strip PAC from pointer to data address */
#define XPACD(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpacd %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with IA key using a modifier */
#define PACIA(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacia %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with IB key using a modifier */
#define PACIB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with DA key using a modifier */
#define PACDA(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacda %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with DB key using a modifier */
#define PACDB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacdb %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)


/**
 * Determine if a pointer contains a PAC signature based on the
 * presence of high bits set in the pointer. Mask should be a
 * 64 bit integer with each bit set beyond the number of bits
 * used for the virtual address space.
 */
#define PACSIGNED(ptr, mask) ((ptr) & (mask))

typedef struct __reg_ctx_t {
        ucontext_t      uc;
        int             stripped_lr;
} reg_ctx_t;

int main(void)
{
        reg_ctx_t ctx;
        getcontext(&ctx.uc);
        ucontext_t uc = ctx.uc;
        mcontext_t mc = ctx.uc.uc_mcontext;

        u32     vabits;
        u64     vamask;
        size_t  size;
        
        int rc = sysctlbyname("machdep.virtual_address_size",
                              &vabits, &size, NULL, 0);
        
        if (rc < 0) {
                rc = sysctlbyname(
                        "machdep.cpu.address_bits.virtual",
                        &vabits, &size, NULL, 0
                );
        }

        if (rc == 0) {
                printf("Virtual address bits: %d\n", vabits);
                vamask = ~((UINT64_C(1) << vabits) - 1);
        } else {
                fprintf(stderr, "Could not find the virtual "
                                "address space size. Assuming "
                                "48 bits\n");
                vamask = ~((UINT64_C(1) << 48) - 1);
        }
        
        printf("original lr:\t%llu\n", mc->__ss.__lr);
        if (PACSIGNED(mc->__ss.__lr, vamask))
                ctx.stripped_lr = 1;
        
        if (ctx.stripped_lr) {
                XPACI(mc->__ss.__lr);
                printf("stripped lr:\t%llu\n",
                        mc->__ss.__lr);
                printf("uc lr:\t%llu\n",
                        uc.uc_mcontext->__ss.__lr);
                assert(!PACSIGNED(mc->__ss.__lr, vamask));
        }
        
        PACIB(mc->__ss.__lr, mc->__ss.__sp);
        printf("re-signed lr:\t%llu\n", mc->__ss.__lr);
        assert(PACSIGNED(mc->__ss.__lr, vamask));

        for (int i = 0; i < 29; i++) {
                if (PACSIGNED(mc->__ss.__x[i], vamask)) {
                        printf("x%d:\t%llu\n",
                                i, mc->__ss.__x[i]);
                }
        }

        if (PACSIGNED(mc->__ss.__sp, vamask))
                printf("signed sp:\t%llu\n", mc->__ss.__sp);
        if (PACSIGNED(mc->__ss.__fp, vamask))
                printf("signed fp:\t%llu\n", mc->__ss.__fp);
        if (PACSIGNED(mc->__ss.__pc, vamask))
                printf("signed pc:\t%llu\n", mc->__ss.__pc);
        
        exit(0);
}
