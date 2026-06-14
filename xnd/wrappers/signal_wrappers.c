/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/pac.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

static struct sigaction                 sa_table[NSIG];
static struct __internal_sigaction      __sigactions[NSIG];

__noreturn void __internal_sigreturn(ucontext_t *ucp)
{
        u64 fp = get_ucontext_fp(ucp);

#if defined(__arm64e__)
        if (PTRAUTH_SIGNED(fp))
                XPACD(fp);
#endif
        pac_resign_frames((u64 *)fp);
        pac_patch_context(ucp);
        setcontext(ucp);
        unreachable();
}

__noreturn void __internal_sigtramp(int sig, siginfo_t *info, void *uctx)
{
        if (__sigactions[sig].sa_siginfo)
                __sigactions[sig].sa_sigaction(sig, info, uctx);
        else
                __sigactions[sig].sa_handler(sig);

        __internal_sigreturn(uctx);
        unreachable();
}

void sig_state_save(void)
{
        for (int sig = 1; sig < NSIG; sig++) {
                if (sig == SIGKILL || sig == SIGSTOP) {
                        continue;
                } if (sigaction(sig, NULL, &sa_table[sig]) < 0) {
                        xnd_warn("sigaction: %s\n", strerror(errno));
                        bzero(&sa_table[sig], sizeof(struct sigaction));
                }
        }
}

void sig_state_restore(void)
{
        for (int sig = 1; sig < NSIG; sig++) {
                if (sig == SIGKILL || sig == SIGSTOP) {
                        continue;
                } else if (sigaction(sig, &sa_table[sig], NULL) < 0) {
                        xnd_warn("sigaction: %s\n", strerror(errno));
                        bzero(&sa_table[sig], sizeof(struct sigaction));
                }
        }
}

sig_t __signal_hook(int sig, sig_t handler)
{
        struct sigaction        hook;
        int                     err;

        if (sig == SIGUSR1 || sig == SIGUSR2) {
                xnd_warn("%s is reserved for libckpt\n",
                         (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                return SIG_ERR;
        } else if (handler == SIG_DFL || handler == SIG_IGN) {
                return signal(sig, handler);
        }
        
        sigemptyset(&hook.sa_mask);
        hook.sa_flags = SA_SIGINFO;
        hook.sa_sigaction = __internal_sigtramp;
        
        err = sigaction(sig, &hook, NULL);
        if (err != 0)
                return SIG_ERR;
        
        __sigactions[sig].sa_handler = handler;
        __sigactions[sig].sa_siginfo = false;
        xnd_trace("Handler installed for signal %d: %p\n", sig, handler);

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
        struct sigaction        hook;
        int                     err;
        char                    *program;
        
        if (act == NULL) {
                return sigaction(sig, NULL, oact);
        } else if (sig == SIGUSR1 || sig == SIGUSR2) {
                xnd_warn("%s is reserved for libckpt\n",
                         (sig == SIGUSR2) ? "SIGUSR2" : "SIGUSR1");
                return -1;
        } else if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
                return sigaction(sig, act, oact);
        }
        
        hook.sa_mask = act->sa_mask;
        hook.sa_flags = act->sa_flags | SA_SIGINFO;
        hook.sa_sigaction = __internal_sigtramp;

        err = sigaction(sig, &hook, oact);
        if (err != 0)
                return err;
        
        program = getenv("XND_PROGRAM");
        xnd_assert(program != NULL);

        if (act->sa_flags & SA_SIGINFO) {
                __sigactions[sig].sa_sigaction = act->sa_sigaction;
                __sigactions[sig].sa_siginfo = true;
                xnd_trace("%s installed handler for signal %d: %p\n",
                          program, sig, act->sa_sigaction);
        } else {
                __sigactions[sig].sa_handler = act->sa_handler;
                __sigactions[sig].sa_siginfo = false;
                xnd_trace("%s installed handler for signal %d: %p\n",
                          program, sig, act->sa_handler);
        }
        
        return err;
}
