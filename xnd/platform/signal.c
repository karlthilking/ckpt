/* signal.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/platform/signal.h"

#define _XOPEN_SOURCE
#include <signal.h>
#include <ucontext.h>

__noreturn void __xnd_sigreturn(ucontext_t *uctx, int ctxstyle,
                                uintptr_t token)
{
        pac_patch_siguctx(uctx);
        pac_resign_frames((u64 *)get_ucontext_fp(uctx));
        __sigreturn(uctx, ctxstyle, token);

        xnd_error("__sigreturn failed!\n");
        xnd_abort();
        unreachable();
}

__noreturn void __xnd_sigtramp(union __sigaction_u __sigaction_u, 
                               int sigstyle, int sig, siginfo_t *info, 
                               ucontext_t *uctx, uintptr_t token)
{
        sa_sigaction(sig, info, uctx);
        __xnd_sigreturn(uctx, UC_FLAVOR, token);
        unreachable();
}

int __xnd_sigaction(int sig, const struct sigaction *act,
                     struct sigaction *oact)
{
        struct __sigaction      nsv, osv;
        int                     err;

        if (act) {
                nsv.sa_mask = act->sa_mask;
                nsv.sa_flags = act->sa_flags;
                nsv.sa_flags &= ~SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP;
                if (act->sa_flags & SA_SIGINFO) {
                        nsv.sa_sigaction = act->sa_sigaction;
                } else {
                        nsv.sa_handler = act->sa_handler;
                }
                nsv.sa_tramp = (void *)__xnd_sigtramp;
                err = __sigaction(sig, &nsv, &osv);
        } else {
                err = __sigaction(sig, NULL, &osv);
        }

        if (err == 0 && oact) {
                oact->sa_mask = osv.sa_mask;
                oact->sa_flags = osv.sa_flags;
                if (oact->sa_flags & SA_SIGINFO) {
                        oact->sa_sigaction = osv.sa_sigaction;
                } else {
                        oact->sa_handler = osv.sa_handler;
                }
        }

        return err;
}
