/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/xnd_lib.h"
#include "xnd/util/env.h"
#include "xnd/platform/signal.h"
#include "xnd/platform/ucontext/ucontext.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

static struct sigaction sa_table[NSIG];

void sig_state_save(void)
{
	int sig;

	for (sig = 1; sig < NSIG; sig++) {
		if (sig == SIGKILL || sig == SIGSTOP)
			continue;
		if (__xnd_sigaction(sig, NULL, &sa_table[sig]) != 0) {
			xnd_warn("Error saving signal disposition "
				 "(signal: %d)\n", sig);
			bzero(&sa_table[sig], sizeof(sa_table[sig]));
		}
	}
}

void sig_state_restore(void)
{
	int sig;

	for (sig = 1; sig < NSIG; sig++) {
		if (sig == SIGKILL || sig == SIGSTOP)
			continue;
		if (__xnd_sigaction(sig, &sa_table[sig], NULL) != 0)
			xnd_warn("Error restoring signal disposition "
				 "(signal: %d)\n", sig);
	}
}

sig_t __signal_hook(int sig, sig_t handler)
{
	struct sigaction sa;

	if (sig == env_get_ckpt_signal()) {
		xnd_warn("Signal %d is reserved\n", sig);
		return SIG_ERR;
	}

	if (handler == SIG_DFL || handler == SIG_IGN)
		return signal(sig, handler);

	/**
	 * Register user signal handlers with __xnd_sigaction
	 * so we can control the signal trampoline function
	 */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = handler;
	if (__xnd_sigaction(sig, &sa, NULL) != 0)
		return SIG_ERR;

	return 0;
}

/**
 * __sigaction_hook:
 *  Protect against users setting up signal handlers for SIGUSR1 or
 *  SIGUSR2; these signals will be reserved for the implementation of xnd.
 */
int __sigaction_hook(int sig, const struct sigaction *act,
                     struct sigaction *oact)
{
	/**
	 * This makes the thread signal handler (in thread_info.c)
	 * not transparent to user threads; maybe should hide the
	 * signal handler?
	 */
	if (act == NULL)
		return sigaction(sig, NULL, oact);

	if (sig == env_get_ckpt_signal()) {
		xnd_warn("Signal %d is reserved\n", sig);
		return -1;
	}

	if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN)
		return sigaction(sig, act, oact);

	/**
	 * If user thread is registering their own signal handler,
	 * call __xnd_sigaction instead of sigaction in order to
	 * avoid libc's signal trampoline (_sigtramp)
	 */
	return __xnd_sigaction(sig, act, oact);
}

int __sigprocmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
        return __pthread_sigmask_hook(how, set, oset);
}

int __pthread_sigmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
	sigset_t clean;
	int ckpt_sig = env_get_ckpt_signal();

	if (set == NULL)
		return pthread_sigmask(how, NULL, oset);

	clean = *set;
	if (how == SIG_BLOCK || how == SIG_SETMASK) {
		if (sigismember(set, ckpt_sig))
			sigdelset(&clean, ckpt_sig);
	}

	return pthread_sigmask(how, &clean, oset);
}

INTERPOSE(__signal_hook, signal);
INTERPOSE(__sigaction_hook, sigaction);
INTERPOSE(__sigprocmask_hook, sigprocmask);
INTERPOSE(__pthread_sigmask_hook, pthread_sigmask);
