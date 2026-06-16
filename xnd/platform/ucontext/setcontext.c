/* xnd_setcontext.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "ucontext.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <signal.h>
#include <errno.h>

__noreturn void xnd_setcontext(ucontext_t *uctx)
{
        int err;

        pac_strip_uctx(uctx);
        err = pthread_sigmask(SIG_SETMASK, &uctx->uc_sigmask, NULL);
        if (err < 0) {
                xnd_warn("pthread_sigmask: %s\n", strerror(err));
        }

        _xnd_setcontext(uctx->uc_mcontext);
        unreachable();
}
