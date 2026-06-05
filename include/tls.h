/* tls.h */
#ifndef __CKPT_TLS_H__
#define __CKPT_TLS_H__
#include "thread_info.h"
#include "types.h"

#define TLS_TLV_FLAG_SLOT       11
#define TLS_TLV_INIT_MAGIC      0x1111000000000000ULL
#define TLS_TLV_EXIT_MAGIC      0xEEEE000000000000ULL

#define PTHREAD_T_TLS_OFFSET                    0xe0
#define PTHREAD_T_CLEANUP_HANDLER_OFFSET        0x8
#define TLS_PTHREAD_T_OFFSET                    -0xe0
#define TLS_CLEANUP_HANDLER_OFFSET              -0xd8

#define force_tlv_init() \
        (void)thread_self_or_null()

#define thread_cleanup_stack(__tls)                     \
        *(struct __darwin_pthread_handler_rec **)       \
         ((char *)(__tls) + TLS_CLEANUP_HANDLER_OFFSET) \

#define thread_cleanup_stack_pointer(__tls)             \
        (struct __darwin_pthread_handler_rec **)        \
        ((char *)(__tls) + TLS_CLEANUP_HANDLER_OFFSET)

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

#endif // __CKPT_TLS_H__
