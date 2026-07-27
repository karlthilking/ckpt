/* xnd_setcontext.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "ucontext.h"

#include <ucontext.h>

__noreturn void xnd_setcontext(ucontext_t *uctx)
{
        pac_strip_uctx(uctx);
        _xnd_setcontext(uctx->uc_mcontext);
        unreachable();
}
