/* tls.h */
#ifndef XND_TLS_H
#define XND_TLS_H
#include "xnd/xnd.h"
#include "xnd/wrappers/pthread_wrappers.h"

extern void _thread_set_tsd_base(void *);

#define assert_pthread_offset(type, field, name) \
	static_assert(offsetof(struct _opaque_##type##_t, field) == \
		      PTHREAD_##name##_OFFSET, "")

#define __TSD_THREAD_SELF 		0
#define __TSD_ERRNO 			1
#define __TSD_MIG_REPLY 		2
#define __TSD_MACH_THREAD_SELF 		3
#define __TSD_THREAD_QOS_CLASS 		4
#define __TSD_RETURN_TO_KERNEL 		5
#define __TSD_PTR_MUNGE 		7
#define __TSD_MACH_SPECIAL_REPLY 	8
#define __TSD_SEMAPHORE_CACHE 		9

#define __TSD_THREAD_SELF_TYPE 		pthread_t
#define __TSD_ERRNO_TYPE                int *
#define __TSD_MIG_REPLY_TYPE            mach_port_t
#define __TSD_MACH_THREAD_SELF_TYPE     mach_port_t
#define __TSD_THREAD_QOS_CLASS_TYPE    	pthread_priority_t
#define __TSD_RETURN_TO_KERNEL_TYPE     uintptr_t
#define __TSD_PTR_MUNGE_TYPE            uintptr_t
#define __TSD_MACH_SPECIAL_REPLY_TYPE   mach_port_t
#define __TSD_SEMAPHORE_CACHE_TYPE      semaphore_t

/*
 * FIXME:
 *  This slot is not currently used by libpthread/libsystem, but would
 *  cause a hard to find bug if ever repurposed for other usage.
 */
#define __TSD_XND_FLAG 6
#define __TSD_XND_INIT 0x0000000005203090ULL

#define PTHREAD_TSD_OFFSET ((intptr_t)224)
#define PTHREAD_THREADID_OFFSET ((intptr_t)216)
#define PTHREAD_SIG_OFFSET ((intptr_t)0)
#define PTHREAD_CLEANUP_HANDLER_OFFSET ((intptr_t)8)

assert_pthread_offset(pthread, __sig, SIG);
assert_pthread_offset(pthread, __cleanup_stack, CLEANUP_HANDLER);

#define TSD_PTHREAD_OFFSET ((intptr_t)-224)
#define TSD_THREADID_OFFSET ((intptr_t)-8)
#define TSD_CLEANUP_HANDLER_OFFSET ((intptr_t)-216)

#define EXTERNAL_POSIX_THREAD_KEYS_MAX 512
#define INTERNAL_POSIX_THREAD_KEYS_MAX 256
#define POSIX_THREAD_KEYS_END \
	(EXTERNAL_POSIX_THREAD_KEYS_MAX + INTERNAL_POSIX_THREAD_KEYS_MAX)

static inline uintptr_t
self_tsd_base(void)
{
	uintptr_t tsd;
	asm volatile("mrs %0, tpidrro_el0" : "=r" (tsd) :: "memory");
	return tsd;
}

static inline uintptr_t
pthread_tsd_base(pthread_t p)
{
	return ((uintptr_t)p + PTHREAD_TSD_OFFSET);
}

#define tsd_relative_access(type, offset) \
	(type *)(self_tsd_base() + offset)
#define tsd_slot_access(type, slot) \
	(type *)(self_tsd_base() + slot * sizeof(void *))

#define pthread_struct_relative_access(p, type, offset)	\
	(type *)(pthread_tsd_base(p) + offset)
#define pthread_struct_slot_access(p, type, slot) \
	(type *)(pthread_tsd_base(p) + slot * sizeof(void *))

static inline u64
self_get_threadid(void)
{
	return *tsd_relative_access(u64, TSD_THREADID_OFFSET);
}

static inline void
self_set_threadid(u64 tid)
{
	*tsd_relative_access(u64, TSD_THREADID_OFFSET) = tid;
}

static inline struct __darwin_pthread_handler_rec **
self_cleanup_stack_addr(void)
{
	struct __darwin_pthread_handler_rec *handler;
	const uintptr_t offset = TSD_CLEANUP_HANDLER_OFFSET;
	return tsd_relative_access(typeof(handler), offset);
}

static inline struct __darwin_pthread_handler_rec *
self_get_cleanup_stack(void)
{
	return *self_cleanup_stack_addr();
}

static inline void
self_set_cleanup_stack(struct __darwin_pthread_handler_rec *handler)
{
	*self_cleanup_stack_addr() = handler;
}

static inline bool
xnd_tlv_ok(void)
{
	u64 flag = *tsd_slot_access(u64, __TSD_XND_FLAG);
	return (flag == __TSD_XND_INIT);
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
static inline void
xnd_tsd_copy(void **dst, void **src)
{
	uint slot;

	xnd_assert(dst != src);
	dst[__TSD_ERRNO] = src[__TSD_ERRNO];
	dst[__TSD_PTR_MUNGE] = src[__TSD_PTR_MUNGE];

	for (slot = 20; slot < POSIX_THREAD_KEYS_END; slot++)
		dst[slot] = src[slot];
}

#ifdef __cplusplus
extern "C" {
#endif

int thread_ptr_munge_save(void);
void thread_ptr_munge_fixup(void);

void xnd_tlv_init(void);
void xnd_tlv_fini(void);

bool validate_tsd_relative_offsets(void);

#ifdef __cplusplus
}
#endif

#endif /* XND_TLS_H */
