/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/xnd_lib.h"
#include "xnd/interpose.h"
#include "xnd/thread_info.h"
#include "xnd/util/env.h"
#include "xnd/platform/signal.h"
#include "xnd/platform/ucontext/ucontext.h"

#include <ucontext.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

static struct sigaction sa_table[SA_TABLE_LEN];

void
sig_state_save(void)
{
        int ret, sig, ckptsig;
        struct sigaction *sa;

	ckptsig = env_get_ckpt_signal();
	if (ckptsig <= 0 || ckptsig >= NSIG ||
	    ckptsig == SIGKILL || ckptsig == SIGSTOP)
		xnd_panic("illegal checkpoint signal: %s\n",
			  strsignal(ckptsig));

	for (sig = 1; sig < NSIG; sig++) {
		if (sig == SIGKILL || sig == SIGSTOP)
			continue;
		sa = &sa_table[SA_TABLE_IDX(sig)];
		ret = xnd_sigaction(sig, NULL, sa);
		if (ret != 0) {
			bzero(sa, sizeof(*sa));
			xnd_warn("error saving signal action (%s)\n",
				 strsignal(sig));
		}
	}

	sa = &sa_table[SA_TABLE_IDX(ckptsig)];
	if (sa->sa_sigaction != thread_sighandler)
		xnd_panic("checkpoint signal action corrupt\n");
}

void
sig_state_restore(void)
{
	int ret, sig, ckptsig;
	struct sigaction *sa;

	ckptsig = env_get_ckpt_signal();
	if (ckptsig <= 0 || ckptsig >= NSIG ||
	    ckptsig == SIGKILL || ckptsig == SIGSTOP)
		xnd_panic("illegal checkpoint signal: %s\n",
			  strsignal(ckptsig));

	sa = &sa_table[SA_TABLE_IDX(ckptsig)];
	if (sa->sa_sigaction != thread_sighandler)
		xnd_panic("checkpoint signal action corrupt\n");

	for (sig = 1; sig < NSIG; sig++) {
		if (sig == SIGSTOP || sig == SIGKILL)
			continue;
		sa = &sa_table[SA_TABLE_IDX(sig)];
		ret = xnd_sigaction(sig, sa, NULL);
		if (ret != 0)
			xnd_warn("error restoring signal action (%s)\n",
				 strsignal(sig));
	}
}

sig_t
signal_hook(int sig, sig_t handler)
{
	int ret;
        struct sigaction sa;

	if (sig == env_get_ckpt_signal()) {
		xnd_warn("%s is reserved\n", strsignal(sig));
		return SIG_ERR;
	}

        if (handler == SIG_DFL || handler == SIG_IGN)
                return signal(sig, handler);

        /*
         * Register user signal handlers with __xnd_sigaction
         * so we can control the signal trampoline function
         */
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sa.sa_handler = handler;

	ret = xnd_sigaction(sig, &sa, NULL);
	return (ret != 0 ? SIG_ERR : 0);
}

/*
 * __sigaction_hook:
 *  Protect against users setting up signal handlers for SIGUSR1 or
 *  SIGUSR2; these signals will be reserved for the implementation of xnd.
 */
int
sigaction_hook(int sig, const struct sigaction *act, struct sigaction *oact)
{
	int ret, ckptsig = env_get_ckpt_signal();

        /*
	 * If a user thread is querying information about the
	 * checkpoint signal, hide the associated signal state.
         */
	if (act == NULL) {
		ret = sigaction(sig, NULL, oact);
		if (sig == ckptsig) {
			sigemptyset(&oact->sa_mask);
			oact->sa_flags = 0;
			oact->sa_handler = SIG_DFL;
		}
		return ret;
	}

	/*
	 * Don't let a user thread establish a different signal
	 * dispostion for the checkpoint signal.
	 */
	if (sig == ckptsig) {
		xnd_warn("%s is reserved\n", strsignal(sig));
		return -1;
	}

	if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN)
		return sigaction(sig, act, oact);

	/*
	 * If a user thread is registering their own signal handler,
	 * route the registery through xnd_sigaction so we can
	 * specify our own signal trampoline (xnd_sigtramp).
	 */
	return xnd_sigaction(sig, act, oact);
}

int
sigprocmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
        return pthread_sigmask_hook(how, set, oset);
}

int
pthread_sigmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
	int ckptsig;
	sigset_t clean;

	if (set == NULL || how == SIG_UNBLOCK)
		return pthread_sigmask(how, set, oset);

	clean = *set;
	ckptsig = env_get_ckpt_signal();
	if (sigismember(set, ckptsig)) {
		xnd_warn("%s should not be masked\n", strsignal(ckptsig));
		sigdelset(&clean, ckptsig);
	}

	return pthread_sigmask(how, &clean, oset);
}

INTERPOSE(signal_hook, signal);
INTERPOSE(sigaction_hook, sigaction);
INTERPOSE(sigprocmask_hook, sigprocmask);
INTERPOSE(pthread_sigmask_hook, pthread_sigmask);
