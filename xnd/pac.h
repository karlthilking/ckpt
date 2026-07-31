/* pac.h */
#ifndef XND_PAC_H
#define XND_PAC_H

#include "xnd/xnd.h"

#include <ucontext.h>
#include <mach/mach.h>
#include <mach/arm/_structs.h>

#if __DARWIN_OPAQUE_ARM_THREAD_STATE64
/**
 * Access/modify ucontext_t, mcontext_t register values for
 * arm64e version of struct __darwin_arm_thread_state64
 * in struct __darwin_mcontext64
 */
#define get_ucontext_lr(uctx) \
	((u64)((uctx)->uc_mcontext->__ss.__opaque_lr))
#define get_ucontext_fp(uctx) \
        ((u64)((uctx)->uc_mcontext->__ss.__opaque_fp))
#define get_ucontext_sp(uctx) \
        ((u64)((uctx)->uc_mcontext->__ss.__opaque_sp))
#define get_ucontext_flags(uctx) \
        ((u32)((uctx)->uc_mcontext->__ss.__opaque_flags))

#define set_ucontext_lr(uctx,lr) \
        ((uctx)->uc_mcontext->__ss.__opaque_lr = (void *)(lr))
#define set_ucontext_fp(uctx, fp) \
        ((uctx)->uc_mcontext->__ss.__opaque_fp = (void *)(fp))
#define set_ucontext_sp(uctx, sp) \
        ((uctx)->uc_mcontext->__ss.__opaque_sp = (void *)(sp))
#define set_ucontext_flags(uctx, flags) \
        ((uctx)->uc_mcontext->__ss.__opaque_flags = (u32)(flags))

#define get_mcontext_lr(mctx) \
        ((u64)((mctx)->__ss.__opaque_lr))
#define get_mcontext_fp(mctx) \
        ((u64)((mctx)->__ss.__opaque_fp))
#define get_mcontext_sp(mctx) \
        ((u64)((mctx)->__ss.__opaque_sp))
#define get_mcontext_flags(mctx)      \
        ((u32)((mctx)->__ss.__opaque_flags))

#define set_mcontext_lr(mctx, lr) \
        ((mctx)->__ss.__opaque_lr = (void *)(lr))
#define set_mcontext_fp(mctx, fp) \
        ((mctx)->__ss.__opaque_fp = (void *)(fp))
#define set_mcontext_sp(mctx, sp) \
        ((mctx)->__ss.__opaque_sp = (void *)(sp))
#define set_mcontext_flags(mctx, flags) \
        ((mctx)->__ss.__opaque_flags = (u32)(flags))

#else /* !__DARWIN_OPAQUE_ARM_THREAD_STATE64 */
/**
 * Access/modify ucontext_t, mcontext_t register values for arm64 version
 * of struct __darwin_arm_thread_state64 in struct __darwin_mcontext64
 */
#define get_ucontext_lr(uctx) \
        ((uctx)->uc_mcontext->__ss.__lr)
#define get_ucontext_fp(uctx) \
        ((uctx)->uc_mcontext->__ss.__fp)
#define get_ucontext_sp(uctx) \
        ((uctx)->uc_mcontext->__ss.__sp)
#define get_ucontext_flags(uctx) \
        ((uctx)->uc_mcontext->__ss.__pad)

#define set_ucontext_lr(uctx, lr) \
        ((uctx)->uc_mcontext->__ss.__lr = (u64)(lr))
#define set_ucontext_fp(uctx, fp) \
        ((uctx)->uc_mcontext->__ss.__fp = (u64)(fp))
#define set_ucontext_sp(uctx, sp) \
        ((uctx)->uc_mcontext->__ss.__sp = (u64)(sp))
#define set_ucontext_flags(uctx, flags) \
        ((uctx)->uc_mcontext->__ss.__pad = (u32)(flags))

#define get_mcontext_lr(mctx) \
        ((mctx)->__ss.__lr)
#define get_mcontext_fp(mctx) \
        ((mctx)->__ss.__fp)
#define get_mcontext_sp(mctx) \
        ((mctx)->__ss.__sp)
#define get_mcontext_flags(mctx) \
        ((mctx)->__ss.__pad)

#define set_mcontext_lr(mctx, lr) \
        ((mctx)->__ss.__lr = (u64)(lr))
#define set_mcontext_fp(mctx, fp) \
        ((mctx)->__ss.__fp = (u64)(fp))
#define set_mcontext_sp(mctx, sp) \
        ((mctx)->__ss.__sp = (u64)(sp))
#define set_mcontext_flags(mctx, flags) \
        ((mctx)->__ss.__pad = (u32)(flags))

#endif

/**
 * Constant discriminator values used for signing and authenticating
 * saved/restored registers in getcontext() and setcontext()
 *
 *  FP_DISCRIMINATOR = ptrauth_string_discriminator("fp") = 17687
 *  SP_DISCRIMINATOR = ptrauth_string_discriminator("sp") = 52205
 *  LR_DISCRIMINATOR = ptrauth_string_discriminator("lr") = 30675
 */
#define FP_DISCRIMINATOR ((u64)0x4517)
#define SP_DISCRIMINATOR ((u64)0xcbed)
#define LR_DISCRIMINATOR ((u64)0x77d3)

/**
 * Flag values/bits in struct __darwin_arm_thread_state64 to manipulate
 * behavior of _setcontext (called from setcontext(ucontext_t *))
 *
 * If (ts->__opaque_flags & LR_SIGNED_WITH_IB), _setcontext will
 * not manually authenticate the link register with its own constant
 * discriminator (assuming it was already signed when obtained from
 * getcontext).
 */
#define LR_SIGNED_WITH_IB                                       0x2
#define LR_SIGNED_WITH_IB_BIT                                   0x1
#define __DARWIN_ARM_THREAD_STATE64_FLAGS_NO_PTRAUTH            0x1
#define __DARWIN_ARM_THREAD_STATE64_FLAGS_IB_SIGNED_LR          0x2
#define __DARWIN_ARM_THREAD_STATE64_FLAGS_KERNEL_SIGNED_PC      0x4
#define __DARWIN_ARM_THREAD_STATE64_FLAGS_KERNEL_SIGNED_LR      0x8
#define __DARWIN_ARM_THREAD_STATE64_USER_DIVERSIFIER_MASK       0xff000000

#define PTRAUTH_BIT_MASK (0xFF7F800000000000ULL)
#define PTRAUTH_SIGNED(ptr) \
	(!!((u64)(ptr) & PTRAUTH_BIT_MASK))

#define PTRAUTH_XPACI(ptr)	      \
	do {			      \
		__asm__ __volatile__( \
			"xpaci %0"    \
			: "+r" (ptr)  \
			:	      \
			: "memory"    \
			);	      \
	} while (0)

#define PTRAUTH_PACIB(ptr, mod)		 \
	do {				 \
		__asm__ __volatile__(	 \
			"pacib %0, %1"	 \
			: "+r" (ptr)	 \
			: "r" ((u64)mod) \
			: "memory"	 \
			);		 \
	} while (0)

#define PTRAUTH_AUTIB(ptr, mod)		 \
	do {				 \
		__asm__ __volatile__(	 \
			"autib %0, %1"	 \
			: "+r" (ptr)	 \
			: "r" ((u64)mod) \
			: "memory"	 \
			);		 \
	} while (0)

#define FRAME_FOR_EACH(fp) \
	for (; (fp) != NULL; (fp) = (u64 *)(fp)[0])

#define FRAME_FOR_EACH_SIGNED(fp)	    \
	for ((fp) = first_signed_frame(fp); \
	     (fp) != NULL;		    \
	     (fp) = next_signed_frame(fp))

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void ptrauth_strip_uctx(ucontext_t *);
void ptrauth_patch_siguctx(ucontext_t *);
void ptrauth_resign_frames(u64 *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_PAC_H */
