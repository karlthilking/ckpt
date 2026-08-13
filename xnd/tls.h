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

/*
 * FIXME:
 *  This slot is not currently used by libpthread/libsystem, but would
 *  cause a hard to find bug if ever repurposed for other usage.
 */
#define __TSD_XND_FLAG 6
#define __TSD_XND_INIT 0x0000000005203090ULL
#define __TSD_XND_FINI 0x0000000020471120ULL

#define PTHREAD_TLS_OFFSET (0x00000000000000E0LL)
#define PTHREAD_CLEANUP_HANDLER_OFFSET (sizeof(long))

#define TLS_PTHREAD_OFFSET (-PTHREAD_TLS_OFFSET)
#define TLS_CLEANUP_HANDLER_OFFSET \
	(TLS_PTHREAD_OFFSET + PTHREAD_CLEANUP_HANDLER_OFFSET)

#define PTHREAD_TSD_END 768

#define get_thread_cleanup_stack(tls)				 \
	({							 \
		*(struct __darwin_pthread_handler_rec **)	 \
		((uintptr_t)(tls) + TLS_CLEANUP_HANDLER_OFFSET); \
	})

#define set_thread_cleanup_stack(tls, handler)			  \
	do {							  \
		*(struct __darwin_pthread_handler_rec **)	  \
		((uintptr_t)(tls) + TLS_CLEANUP_HANDLER_OFFSET) = \
		(struct __darwin_pthread_handler_rec *)(handler); \
	} while (0)

static __always_inline uintptr_t *tls_slot_location(uint slot)
{
	uintptr_t tls;

	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
	return (uintptr_t *)(tls + slot * sizeof(void *));
}

#define get_tls_slot(slot)		    \
	({				    \
		*(tls_slot_location(slot)); \
	})

#define set_tls_slot(slot, val)			    \
	do {					    \
		*(tls_slot_location(slot)) = (val); \
	} while (0)

static inline void xnd_tlv_init(void)
{
	(void)thread_self_or_null();
	set_tls_slot(__TSD_XND_FLAG, __TSD_XND_INIT);
}

static inline void xnd_tlv_fini(void)
{
	set_tls_slot(__TSD_XND_FLAG, __TSD_XND_FINI);
}

static inline bool xnd_tlv_ok(void)
{
	return get_tls_slot(__TSD_XND_FLAG) == __TSD_XND_INIT;
}

/*
 * Posix thread keys:
 *  [0,     9] libsyscall/libplatorm
 *  [10,   18] libc/libsystem
 *  [20,   29] libdispatch
 *  [30,   39] graphic frameworks
 *  [40,   49] Objective-C runtime
 *  [50,   59] Core Foundation
 *  [60,   69] Foundation
 *  [70,   79] Core Animation/Quartz Core
 *  [80,   89] CoreData
 *  [90,   94] JavaScriptCore Collection
 *  [      95] CoreText
 *  [100, 109] Swift Runtime
 *  [110, 114] libmalloc
 *  [115, 124] libdispatch workgroups
 *  [125, 209] __thread support
 *  [210, 216] iOS simulator libSystem
 *  [217, 229] simulator libSystem
 *  [     230] Objective-C trace
 *  [231, 232] libsanitizers
 */
static inline void xnd_tsd_copy(void **dst, void **src)
{
	uint slot;

	dst[__TSD_ERRNO] = src[__TSD_ERRNO];
	dst[__TSD_PTR_MUNGE] = src[__TSD_PTR_MUNGE];

	for (slot = 20; slot < PTHREAD_TSD_END; slot++)
		dst[slot] = src[slot];
}

#ifdef __cplusplus
extern "C" {
#endif

int thread_ptr_munge_save(void);
void thread_ptr_munge_fixup(void);

#ifdef __cplusplus
}
#endif

#endif /* XND_TLS_H */
