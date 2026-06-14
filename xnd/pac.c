/* pac.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/util/log.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

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

static __always_inline void ucontext_populate_mctx(ucontext_t *uctx)
{
        if (uctx->uc_mcontext == &uctx->__mcontext_data)
                return;
        memmove(&uctx->__mcontext_data, uctx->uc_mcontext,
                sizeof(uctx->__mcontext_data));
        uctx->uc_mcontext = &uctx->__mcontext_data;
}

/**
 * pac_patch_ucontext:
 *  Patch ucontext pointer that was delivered in signal
 *  handler frame during checkpoint.
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
        
        ucontext_populate_mctx(uctx);
}

void pac_resign_frames(u64 *fp)
{
        for_each_signed_frame(fp) {
                xnd_assert(PTRAUTH_SIGNED(fp[1]));
                XPACI(fp[1]);
                PACIB(fp[1], (u64)fp + 0x10);
        }
}
