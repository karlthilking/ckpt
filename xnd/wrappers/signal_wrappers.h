/* signal_wrappers.h */
#ifndef SIGNAL_WRAPPERS_H
#define SIGNAL_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/inject.h"

#define _XOPEN_SOURCE
#include <signal.h>
#include <ucontext.h>

typedef void (*sig_t)(int);

struct __real_sigaction {
        union __sigaction_u     __sigaction_u;
        bool                    sa_siginfo;
};

void __internal_sigreturn(ucontext_t *);
void __internal_sigtramp(int, siginfo_t *, void *);

void sig_state_save(void);
void sig_state_restore(void);

extern sig_t  signal(int, sig_t);
extern int    sigaction(int, const struct sigaction *, struct sigaction *);

sig_t  __signal_hook(int, sig_t);
int    __sigaction_hook(int, const struct sigaction *, struct sigaction *);

INTERPOSE(__signal_hook, signal);
INTERPOSE(__sigaction_hook, sigaction);

#endif // SIGNAL_WRAPPERS_H
