/* signal.h */
#ifndef XND_SIGNAL_H
#define XND_SIGNAL_H

#include "xnd/xnd.h"

#include <signal.h>
#include <ucontext.h>

#define UC_TRAD         1
#define UC_FLAVOR       30

#define SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP 0x0400

#define sigandset(x, y, z)						\
	do {								\
		sigemptyset(x);						\
		for (int __s = 1; __s < NSIG; __s++) {			\
			if (__s == SIGSTOP || __s == SIGKILL)		\
				continue;				\
			if (sigismember(y, __s) && sigismember(z, __s)) \
				sigaddset(x, __s);			\
		}							\
	} while (0)

extern int __sigreturn(ucontext_t *, int, uintptr_t);
extern int __sigaction(int, struct __sigaction *, struct __sigaction *);

int __xnd_sigaction(int, const struct sigaction *, struct sigaction *);
void __xnd_sigreturn(ucontext_t *, int, uintptr_t);
void __xnd_sigtramp(union __sigaction_u, int, int, siginfo_t *,
                    ucontext_t *, uintptr_t);

#endif /* XND_SIGNAL_H */
