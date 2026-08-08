/* xnd_setcontext.c */
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "ucontext.h"

#include <ucontext.h>

__noreturn void xnd_setcontext(ucontext_t *uctx)
{
	ptrauth_strip_uctx(uctx);
	_xnd_setcontext(uctx->uc_mcontext);

	xnd_error("fatal error: _xnd_setcontext failed\n");
	xnd_abort();

	unreachable();
}
