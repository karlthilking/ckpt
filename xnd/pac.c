/* pac.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/util/log.h"

#include <ucontext.h>
#include <stdlib.h>

static __always_inline u64 *first_signed_frame(u64 *fp)
{
        u64 *__fp;

        for (__fp = fp; __fp != NULL; __fp = (u64 *)__fp[0]) {
                if (PTRAUTH_SIGNED(__fp[1]))
                        return __fp;
        }

        return NULL;
}

static __always_inline u64 *next_signed_frame(u64 *fp)
{
        u64 *__fp = fp;

        if (!__fp)
                return NULL;

        for (__fp = (u64 *)fp[0]; __fp != NULL; __fp = (u64 *)__fp[0]) {
                if (PTRAUTH_SIGNED(__fp[1]))
                        return __fp;
        }

        return NULL;
}

static __always_inline void uctx_populate_mctx(ucontext_t *uctx)
{
        if (uctx->uc_mcontext != &uctx->__mcontext_data) {
                memcpy(&uctx->__mcontext_data, uctx->uc_mcontext,
                       sizeof(struct __darwin_mcontext64));
                uctx->uc_mcontext = &uctx->__mcontext_data;
        }
}

/**
 * pac_strip_uctx:
 *  Unconditionally strip any PAC signatures in a register
 *  context. Only the link register should be signed under
 *  normal conditions.
 *
 * (frame pointer and stack pointer will be signed if obtained
 *  via getcontext in an arm64e process)
 */
void pac_strip_uctx(ucontext_t *uctx)
{
        u64 fp, lr, sp;

        lr = get_ucontext_lr(uctx);
        if (PTRAUTH_SIGNED(lr)) {
                XPACI(lr);
                set_ucontext_lr(uctx, lr);
        }

        fp = get_ucontext_fp(uctx);
        sp = get_ucontext_sp(uctx);
#if defined(__arm64e__)
        if (PTRAUTH_SIGNED(fp)) {
                XPACD(fp);
                set_ucontext_fp(uctx, fp);
        }
        if (PTRAUTH_SIGNED(sp)) {
                XPACD(sp);
                set_ucontext_sp(uctx, sp);
        }
#else
        xnd_assert(!PTRAUTH_SIGNED(fp));
        xnd_assert(!PTRAUTH_SIGNED(sp));
#endif
}

/**
 * pac_patch_siguctx:
 *  Patching a user context obtained from a signal handler
 *  frame should be handled differently than patching a user
 *  context obtained via getcontext (pac_patch_context).
 *
 *  Re-signing a user context that was obtained in a signal
 *  handler should use fp + 0x10 as the discriminator and
 *  the IB key:
 *    pacib(lr, fp + 0x10)
 */
void pac_patch_siguctx(ucontext_t *uctx)
{
        u64 lr = get_ucontext_lr(uctx);
        if (PTRAUTH_SIGNED(lr)) {
                XPACI(lr);
                PACIB(lr, get_ucontext_fp(uctx) + 0x10);
                set_ucontext_lr(uctx, lr);
        }
}

/**
 * pac_patch_ucontext:
 *  Patch a user context obtained via getcontext so that
 *  it can be used in a call to setcontext.
 */
void pac_patch_context(ucontext_t *uctx)
{
        u64 lr, fp, sp;

        /**
         * Unconditionally strip and resign link register
         * with constant discriminator used for auth in
         * _setcontext.
         *
         * By setting thread state flags to 0, _setcontext
         * will take the path of authenticating the link
         * register against its own discriminator, and then
         * will manually re-sign the link register with the
         * correct stack pointer value.
         */
        lr = get_ucontext_lr(uctx);
        XPACI(lr);
        PACIA(lr, LR_DISCRIMINATOR);
        set_ucontext_lr(uctx, lr);
        set_ucontext_flags(uctx, 0);

        fp = get_ucontext_fp(uctx);
        XPACD(fp);
        PACDA(fp, FP_DISCRIMINATOR);
        set_ucontext_fp(uctx, fp);

        sp = get_ucontext_sp(uctx);
        XPACD(sp);
        PACDA(sp, SP_DISCRIMINATOR);
        set_ucontext_sp(uctx, sp);

        uctx_populate_mctx(uctx);
}

void pac_resign_frames(u64 *fp)
{
        u64 __fp = (u64)fp;
        if (PTRAUTH_SIGNED(__fp)) {
                XPACD(__fp);
                fp = (u64 *)__fp;
        }

        for_each_signed_frame(fp) {
                xnd_assert(PTRAUTH_SIGNED(fp[1]));
                XPACI(fp[1]);
                PACIB(fp[1], (u64)fp + 0x10);
        }
}
