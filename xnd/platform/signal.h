/* signal.h */
#ifndef XND_SIGNAL_H
#define XND_SIGNAL_H

#include "xnd/xnd.h"

#include <signal.h>
#include <ucontext.h>

#define UC_TRAD 1
#define UC_FLAVOR 30

#define SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP 0x0400

#define SIGTERMSET (sigmask(SIGINT) | sigmask(SIGTERM) | sigmask(SIGQUIT))
#define SIGCANTSET (sigmask(SIGKILL) | sigmask(SIGKILL))

#define sigandset(ret, s1, s2)				\
	do {						\
		sigset_t __ret;				\
		sigemptyset(&__ret);			\
		for (int sig = 1; sig < NSIG; sig++) {	\
			if (sigismember(s1, sig) &&	\
			    sigismember(s2, sig))	\
				sigaddset(&__ret, sig); \
		}					\
		*(ret) = __ret;				\
	} while (0)

#define sigsetequal(s1, s2)			       \
	({					       \
		bool __eq = true;		       \
		for (int sig = 1; sig < NSIG; sig++) { \
			if (sigismember(s1, sig) !=    \
			    sigismember(s2, sig)) {    \
				__eq = false;	       \
				break;		       \
			}			       \
		}				       \
		__eq;				       \
	})

int xnd_sigaction(int, const struct sigaction *, struct sigaction *);

#endif /* XND_SIGNAL_H */
