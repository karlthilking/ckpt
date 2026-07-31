/* tls.h */
#ifndef XND_TLS_H
#define XND_TLS_H
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/thread_info.h"
#include "xnd/wrappers/pthread_wrappers.h"

#define __TSD_THREAD_SELF 0
#define __TSD_ERRNO 1
#define __TSD_MIG_REPLY 2
#define __TSD_MACH_THREAD_SELF 3
#define __TSD_THREAD_QOS_CLASS 4
#define __TSD_RETURN_TO_KERNEL 5
#define __TSD_PTR_MUNGE 7
#define __TSD_MACH_SPECIAL_REPLY 8
#define __TSD_SEMAPHORE_CACHE 9

#define TLS_TLV_FLAG_SLOT       6
#define TLS_TLV_INIT_MAGIC      0x1111000000000000ULL
#define TLS_TLV_EXIT_MAGIC      0xEEEE000000000000ULL

#define PTHREAD_T_TLS_OFFSET                    0xe0
#define PTHREAD_T_CLEANUP_HANDLER_OFFSET        0x8
#define TLS_PTHREAD_T_OFFSET                    -0xe0
#define TLS_CLEANUP_HANDLER_OFFSET              -0xd8

#define TSD_SLOTS 768

#define force_tlv_init() do { (void)thread_self_or_null(); } while (0)

#define get_thread_cleanup_stack(__tls)                         \
({                                                              \
        *(struct __darwin_pthread_handler_rec **)               \
         ((uintptr_t)(__tls) + TLS_CLEANUP_HANDLER_OFFSET);     \
})

#define set_thread_cleanup_stack(__handler)                     \
({                                                              \
        *(struct __darwin_pthread_handler_rec **)               \
         ((uintptr_t)pthread_self() +                           \
         PTHREAD_T_CLEANUP_HANDLER_OFFSET) =                    \
         (struct __darwin_pthread_handler_rec *)(__handler);    \
})

#define get_pthread_tls(p)                      \
({                                              \
        (uintptr_t)(p) + PTHREAD_T_TLS_OFFSET;  \
})

#define get_thread_info_tls(th)                         \
({                                                      \
        ((uintptr_t)(th)->self) + PTHREAD_T_TLS_OFFSET; \
})

#define get_pthread_mach_port(p)                                        \
({                                                                      \
        uintptr_t __tls, __slot;                                        \
        __tls = get_pthread_tls(p);                                     \
        __slot = (uintptr_t)((void **)__tls)[__TSD_MACH_THREAD_SELF];   \
        (mach_port_t)__slot;                                            \
})

#define get_thread_info_mach_port(th)           \
({                                              \
        get_pthread_mach_port((th)->self);      \
})

static __always_inline void set_tls_slot(uint __slot, uintptr_t __val)
{
        uintptr_t __tls;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (__tls) :: "memory");
        *(uintptr_t *)(__tls + __slot * sizeof(void *)) = __val;
}

static __always_inline uintptr_t get_tls_slot(uint __slot)
{
        uintptr_t __tls;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (__tls) :: "memory");
        return *(uintptr_t *)(__tls + __slot * sizeof(void *));
}

static __always_inline void tlv_init(void)
{
        force_tlv_init();
        set_tls_slot(TLS_TLV_FLAG_SLOT, TLS_TLV_INIT_MAGIC);
}

static __always_inline void tlv_exit(void)
{
        set_tls_slot(TLS_TLV_FLAG_SLOT, TLS_TLV_EXIT_MAGIC);
}

static __always_inline bool tlv_ok(void)
{
        return get_tls_slot(TLS_TLV_FLAG_SLOT) == TLS_TLV_INIT_MAGIC;
}

static __always_inline uintptr_t thread_munge_token(void)
{
        uintptr_t tls, self, sig;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        self = get_tls_slot(__TSD_THREAD_SELF);
        sig = *(uintptr_t *)self;

#if defined(__arm64e__)
        PACDB(self, PTHREAD_SELF_DISCRIMINATOR);
#endif
        return sig ^ self;
}

static __always_inline void thread_sig_fixup(uintptr_t munge_token)
{
        uintptr_t       self, tls;
        long            sig;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        self = get_tls_slot(__TSD_THREAD_SELF);
        sig = *(long *)self;

        if ((sig ^ munge_token) != self) {
#if defined(__arm64e__)
                PACDB(self, PTHREAD_SELF_DISCRIMINATOR);
                sig = self ^ munge_token;
                XPACD(self);
#else
                sig = self ^ munge_token;
#endif
                *(long *)self = sig;
                set_tls_slot(__TSD_PTR_MUNGE, munge_token);
        }
}

#endif /* XND_TLS_H */
