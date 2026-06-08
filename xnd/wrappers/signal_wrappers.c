/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/pac.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

static struct sigaction         sa_table[NSIG];
static struct __real_sigaction  __real_sigactions[NSIG];

__noreturn void __internal_sigreturn(ucontext_t *ucp)
{
        u64 fp;

        pac_patch_context(ucp);
        fp = get_ucontext_fp(ucp);
        
        if (PTRAUTH_SIGNED(fp)) {
                XPACD(fp);
        }

        pac_resign_frames((u64 *)fp);
        setcontext(ucp);

        unreachable();
}

void __internal_sigtramp(int sig, siginfo_t *info, void *uctx)
{
        if (__real_sigactions[sig].sa_siginfo) {
                __real_sigactions[sig].sa_sigaction(sig, info, uctx);
        } else {
                __real_sigactions[sig].sa_handler(sig);
        }

        __internal_sigreturn(uctx);
        unreachable();
}

void sig_state_save(void)
{
        for (int signo = 1; signo < NSIG; signo++) {
                if (signo == SIGKILL || signo == SIGSTOP) {
                        continue;
                } else if (sigaction(signo, NULL, &sa_table[signo]) < 0) {
                        warn("sigaction(%d, ...)", signo);
                        bzero(&sa_table[signo], sizeof(struct sigaction));
                }
        }
}

void sig_state_restore(void)
{
        for (int signo = 1; signo < NSIG; signo++) {
                if (signo == SIGKILL || signo == SIGSTOP) {
                        continue;
                } else if (sigaction(signo, &sa_table[signo], NULL) < 0) {
                        warn("sigaction(%d, ...)", signo);
                }
        }
}

sig_t __signal_hook(int sig, sig_t handler)
{
        struct sigaction        hook;
        int                     err;

        if (sig == SIGUSR1 || sig == SIGUSR2) {
                fprintf(stderr, "%s is reserved for libckpt\n",
                        (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                errno = EINVAL;
                return SIG_ERR;
        }
        
        if (handler == SIG_DFL || handler == SIG_IGN) {
                return signal(sig, handler);
        }
        
        sigemptyset(&hook.sa_mask);
        hook.sa_flags = SA_SIGINFO;
        hook.sa_sigaction = __internal_sigtramp;
        
        err = sigaction(sig, &hook, NULL);
        if (err != 0) {
                return SIG_ERR;
        }

        __real_sigactions[sig].sa_handler = handler;
        __real_sigactions[sig].sa_siginfo = false;

        return handler;
}

/**
 * __sigaction_hook:
 *  Protect against users setting up signal handlers for SIGUSR1 or
 *  SIGUSR2; these signals will be reserved for the implementation of
 *  libckpt.
 *
 *  Secondly, to avoid the _sigtramp to __sigreturn failure path, user
 *  signal handlers will be called with an libckpt internal signal
 *  trampoline with calls setcontext() to avoid to __sigreturn path.
 *
 *  Therefore, whenever a user calls sigaction() to set up a handler
 *  or sigaction for a signal (using a user handler), the real signal
 *  handler will be saved, but sigaction will be called with the internal
 *  signal trampoline that protects from __sigreturn.
 */
int __sigaction_hook(int sig, const struct sigaction *act,
                     struct sigaction *oact)
{
        struct sigaction hook;
        
        if (act == NULL) {
                return sigaction(sig, act, oact);
        } else if (sig == SIGUSR1 || sig == SIGUSR2) {
                fprintf(stderr, "%s is reserved for libckpt\n",
                        (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                return -1;
        }

        if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
                return sigaction(sig, act, oact);
        }
        
        hook.sa_mask = act->sa_mask;
        hook.sa_flags = act->sa_flags | SA_SIGINFO;
        hook.sa_sigaction = __internal_sigtramp;

        if (act->sa_flags & SA_SIGINFO) {
                __real_sigactions[sig].sa_sigaction = act->sa_sigaction;
                __real_sigactions[sig].sa_siginfo = true;
        } else {
                __real_sigactions[sig].sa_handler = act->sa_handler;
                __real_sigactions[sig].sa_siginfo = false;
        }

        return sigaction(sig, &hook, oact);
}
