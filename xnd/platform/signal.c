/* signal.c */
#include <errno.h>
#include <signal.h>
#include <ucontext.h>

#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/platform/signal.h"

__noreturn void __xnd_sigreturn(ucontext_t *uctx, int ctxstyle,
				uintptr_t token)
{
	u64 *fp = (u64 *)get_ucontext_fp(uctx);

	ptrauth_patch_siguctx(uctx);
	ptrauth_resign_frames(fp);

	__sigreturn(uctx, ctxstyle, token);
	xnd_error("fatal error: __sigreturn fallthrough\n");

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
	int ret;
	struct __sigaction nsv, osv;

	if (sig <= 0 || sig >= NSIG) {
		errno = EINVAL;
		return -1;
	}

	if (act != NULL && (sig == SIGSTOP || sig == SIGKILL)) {
		errno = EINVAL;
		return -1;
	}

	if (act != NULL) {
		nsv.sa_mask = act->sa_mask;
		nsv.sa_flags = act->sa_flags;
		nsv.sa_flags &= ~SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP;
		nsv.sa_handler = act->sa_handler;
		nsv.sa_tramp = (void *)__xnd_sigtramp;
		ret = __sigaction(sig, &nsv, &osv);
	} else {
		ret = __sigaction(sig, NULL, &osv);
	}

	if (ret == 0 && oact != NULL) {
		oact->sa_mask = osv.sa_mask;
		oact->sa_flags = osv.sa_flags;
		oact->sa_handler = osv.sa_handler;
	}

        return ret;
}
