/* signal_wrappers.h */
#ifndef SIGNAL_WRAPPERS_H
#define SIGNAL_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/inject.h"
#include "xnd/platform/signal.h"

#define _XOPEN_SOURCE
#include <signal.h>
#include <ucontext.h>

typedef void (*sig_t)(int);

struct __internal_sigaction {
        union __sigaction_u     __sigaction_u;
        bool                    sa_siginfo;
};

void sig_state_save(void);
void sig_state_restore(void);

sig_t __signal_hook(int, sig_t);
int __sigaction_hook(int, const struct sigaction *, struct sigaction *);
int __sigprocmask_hook(int, const sigset_t *, sigset_t *);
int __pthread_sigmask_hook(int, const sigset_t *, sigset_t *);

#endif // SIGNAL_WRAPPERS_H
