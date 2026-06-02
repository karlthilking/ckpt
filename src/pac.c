/* pac.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <ucontext.h>
#include "types.h"
#include "pac.h"

static __always_inline u64 *first_signed_frame(u64 *fp)
{
        for (; fp != NULL; fp = (u64 *)fp[0]) {
                if (PTRAUTH_SIGNED(fp[1])) {
                        return fp;
                }
        }

        return NULL;
}

static __always_inline u64 *next_signed_frame(u64 *fp)
{
        for (fp = (u64 *)fp[0]; fp != NULL; fp = (u64 *)fp[0]) {
                if (PTRAUTH_SIGNED(fp[1])) {
                        return fp;
                }
        }

        return NULL;
}

/**
 * pac_patch_ucontext:
 *  Patch ucontext pointer that was delivered in signal
 *  handler frame during checkpoint.
 */
void pac_patch_context(ckpt_context_t *ctx)
{
        u64 fp = get_ucontext_fp(ctx);
        u64 lr = get_ucontext_lr(ctx);
        u64 sp = get_ucontext_sp(ctx);
        
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
        XPACI(lr);
        PACIA(lr, LR_DISCRIMINATOR);
        set_ucontext_lr(ctx, lr);
        set_ucontext_flags(ctx, 0);
        
        XPACD(fp);
        PACDA(fp, FP_DISCRIMINATOR);
        set_ucontext_fp(ctx, fp);
        
        XPACD(sp);
        PACDA(sp, SP_DISCRIMINATOR);
        set_ucontext_sp(ctx, sp);

        /**
         * Copy the context pointed to by ucp->uc_mcontext
         * to ucp->__mcontext_data because setcontext()
         * will access the register context through
         * __mcontext_data.
         */
        if (ctx->uc_mcontext != (mcontext_t)&ctx->__mcontext_data) {
                memmove(&ctx->__mcontext_data, ctx->uc_mcontext,
                        sizeof(ctx->__mcontext_data));
        }
}

void pac_resign_frames(u64 *fp)
{
        for_each_signed_frame(fp) {
                assert(PTRAUTH_SIGNED(fp[1]));
                XPACI(fp[1]);
                PACIB(fp[1], (u64)fp + 0x10);
        }
}
