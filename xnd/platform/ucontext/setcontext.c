/* xnd_setcontext.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "ucontext.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <errno.h>

__noreturn void xnd_setcontext(ucontext_t *uctx)
{
        int     err;
        u64     fp, lr, sp;
        
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
        
        err = pthread_sigmask(SIG_SETMASK, &uctx->uc_sigmask, NULL);
        if (err < 0) {
                xnd_warn("pthread_sigmask: %s\n", strerror(err));
        }

        _xnd_setcontext(uctx->uc_mcontext);
        unreachable();
}
