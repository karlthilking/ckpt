/* pac.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/util/log.h"

#include <ucontext.h>
#include <stdlib.h>

static __always_inline u64 *first_signed_frame(u64 *fp)
{
	FRAME_FOR_EACH(fp) {
		if (PTRAUTH_SIGNED(fp[1])) {
			return fp;
		}
	}

	return NULL;
}

static __always_inline u64 *next_signed_frame(u64 *fp)
{
	if (fp == NULL) {
		return NULL;
	}

	for (fp = (u64 *)fp[0]; fp != NULL; fp = (u64 *)fp[0]) {
		if (PTRAUTH_SIGNED(fp[1])) {
			return fp;
		}
	}

	return NULL;
}

/**
 * ptrauth_strip_uctx:
 *  Strip pac signature from link register if signed
 */
void ptrauth_strip_uctx(ucontext_t *uctx)
{
	u64 lr = get_ucontext_lr(uctx);

	if (PTRAUTH_SIGNED(lr)) {
		PTRAUTH_XPACI(lr);
		set_ucontext_lr(uctx, lr);
	}
}

/**
 * ptrauth_patch_siguctx:
 *  Patch a user context pushed into a signal handler frame.
 *  If the link register was signed, strip and resign with
 *  fp + 0x10 (value of stack pointer at function entry).
 */
void ptrauth_patch_siguctx(ucontext_t *uctx)
{
	u64 lr, fp;

	lr = get_ucontext_lr(uctx);
	if (PTRAUTH_SIGNED(lr)) {
		fp = get_ucontext_fp(uctx);
		PTRAUTH_XPACI(lr);
		PTRAUTH_PACIB(lr, fp + 0x10);
		set_ucontext_lr(uctx, lr);
	}
}

/**
 * ptrauth_resign_frames:
 *  Walk each frame on the stack, and strip + re-sign any link
 *  register on the stack that had a PAC signature before
 *  restarting.
 */
void ptrauth_resign_frames(u64 *fp)
{
	u64 sp;

	FRAME_FOR_EACH_SIGNED(fp) {
		sp = (u64)fp + 0x10;
		xnd_assert(PTRAUTH_SIGNED(fp[1]));
		PTRAUTH_XPACI(fp[1]);
		PTRAUTH_PACIB(fp[1], sp);
	}
}
