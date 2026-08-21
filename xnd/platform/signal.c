/* signal.c */
#include <errno.h>
#include <signal.h>
#include <ucontext.h>

#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/platform/signal.h"

extern int __sigwait(const sigset_t *, int *);
extern int __sigreturn(ucontext_t *, int, uintptr_t);
extern int __sigaction(int, struct __sigaction *, struct __sigaction *);

static void xnd_sigreturn(ucontext_t *, int, uintptr_t) __noreturn;
static void xnd_sigtramp(union __sigaction_u, int, int, siginfo_t *,
			 ucontext_t *, uintptr_t) __noreturn;

/*
 * xnd_sigreturn:
 *  Re-sign signal frame user context and pac-signed return addresses
 *  before calling __sigreturn.
 */
static void
xnd_sigreturn(ucontext_t *uctx, int ctxstyle, uintptr_t token)
{
	u64 *fp = (u64 *)get_ucontext_fp(uctx);

	ptrauth_patch_siguctx(uctx);
	ptrauth_resign_frames(fp);

	__sigreturn(uctx, ctxstyle, token);
	xnd_panic("fatal error: __sigreturn fallthrough\n");

	unreachable();
}

/*
 * xnd_sigtramp:
 *  Invoke user signal handler and use xnd_sigreturn to restore
 *  current thread through PAC-aware return path.
 */
static void
xnd_sigtramp(union __sigaction_u __sigaction_u, int sigstyle, int sig,
	     siginfo_t *sinfo, ucontext_t *uctx, uintptr_t token)
{
	sa_sigaction(sig, sinfo, uctx);
	xnd_sigreturn(uctx, UC_FLAVOR, token);
	unreachable();
}

int
xnd_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	int ret = 0;
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
		nsv.sa_flags = act->sa_flags;
		nsv.sa_flags &= ~SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP;
		nsv.sa_mask = act->sa_mask;
		nsv.sa_handler = act->sa_handler;
		nsv.sa_tramp = (void *)xnd_sigtramp;
		ret = __sigaction(sig, &nsv, &osv);
	} else if (oact != NULL) {
		ret = __sigaction(sig, NULL, &osv);
	}

	if (oact != NULL && ret == 0) {
		oact->sa_mask = osv.sa_mask;
		oact->sa_flags = osv.sa_flags;
		oact->sa_handler = osv.sa_handler;
	}

	if (ret != 0) {
		errno = ret;
		ret = -1;
	}

        return ret;
}
