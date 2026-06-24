/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/xnd_lib.h"
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
        int err;

        for (int sig = 1; sig < NSIG; sig++) {
                if (sig == SIGKILL || sig == SIGSTOP) {
                        continue;
                } 
                err = __xnd_sigaction(sig, NULL, &sa_table[sig]);
                if (err != 0) {
                        xnd_warn("Failed to save signal disposition "
                                 "for signal %d\n", sig);
                        bzero(&sa_table[sig], sizeof(struct sigaction));
                }
        }
}

void sig_state_restore(void)
{
        int err;

        for (int sig = 1; sig < NSIG; sig++) {
                if (sig == SIGKILL || sig == SIGSTOP) {
                        continue;
                } 
                err = __xnd_sigaction(sig, &sa_table[sig], NULL);
                if (err != 0) {
                        xnd_warn("Failed to restore signal disposition "
                                 "for signal %d\n", sig);
                }
        }
}

sig_t __signal_hook(int sig, sig_t handler)
{
        struct sigaction        sa;
        int                     err;
        char                    *program;

        if (sig == SIGUSR1 || sig == SIGUSR2) {
                xnd_warn("signal %d is reserved (%s)\n",
                         sig, (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                return SIG_ERR;
        } else if (handler == SIG_DFL || handler == SIG_IGN) {
                return signal(sig, handler);
        }
        
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = handler;
        err = __xnd_sigaction(sig, &sa, NULL);
        if (err != 0) {
                return SIG_ERR;
        }
        
        if ((program = xnd_program()) != NULL) {
                xnd_trace("%s installed handler for signal %d: %p\n",
                          program, sig, handler);
        }
        
        return handler;
}

/**
 * __sigaction_hook:
 *  Protect against users setting up signal handlers for SIGUSR1 or
 *  SIGUSR2; these signals will be reserved for the implementation of xnd.
 */
int __sigaction_hook(int sig, const struct sigaction *act,
                     struct sigaction *oact)
{
        int     err;
        char    *program;
        
        if (!act) {
                return sigaction(sig, NULL, oact);
        }

        if (sig == SIGUSR1 || sig == SIGUSR2) {
                xnd_warn("signal %d is reserved (%s)\n",
                         sig, (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                return -1;
        }

        if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
                return sigaction(sig, act, oact);
        }
        
        err = __xnd_sigaction(sig, act, oact);
        if (err != 0) {
                return err;
        }
        
        if ((program = xnd_program()) != NULL) {
                xnd_trace("%s installed handler for signal %d: %p\n",
                          program, sig, act->sa_handler);
        }
        
        return err;
}

int __sigprocmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
        return __pthread_sigmask_hook(how, set, oset);
}

int __pthread_sigmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
        sigset_t clean;

        if (!set) {
                return pthread_sigmask(how, NULL, oset);
        }

        if (sigismember(set, SIGUSR1) || sigismember(set, SIGUSR2)) {
                xnd_trace("User thread is attempting to manipulate "
                          "a xnd reserved signal number\n");
        }
        
        clean = *set;
        switch (how) {
        case SIG_BLOCK:
                if (sigismember(set, SIGUSR1)) {
                        xnd_trace("SIGUSR1 can not be blocked for the "
                                  "implementation of xnd\n");
                        sigdelset(&clean, SIGUSR1);
                }
                break;
        case SIG_UNBLOCK:
                if (sigismember(set, SIGUSR2)) {
                        xnd_trace("SIGUSR2 must be kept blocked for the"
                                  "implementation of xnd\n");
                        sigdelset(&clean, SIGUSR2);
                }
                break;
        case SIG_SETMASK:
                if (sigismember(set, SIGUSR1)) {
                        xnd_trace("SIGUSR1 can not be blocked for the "
                                  "implementation of xnd\n");
                        sigdelset(&clean, SIGUSR1);
                }
                if (!sigismember(set, SIGUSR2)) {
                        xnd_trace("SIGUSR2 must be kept blocked for the "
                                  "implementation of xnd\n");
                        sigaddset(&clean, SIGUSR2);
                }
                break;
        }
        
        return pthread_sigmask(how, &clean, oset);
}

INTERPOSE(__signal_hook, signal);
INTERPOSE(__sigaction_hook, sigaction);
INTERPOSE(__sigprocmask_hook, sigprocmask);
INTERPOSE(__pthread_sigmask_hook, pthread_sigmask);
