/* ckpt.h */
#ifndef XND_LIB_H
#define XND_LIB_H

#include "xnd/xnd.h"
#include <sys/types.h>

enum xnd_state {
        XND_UNINITIALIZED,
        XND_RUNNING,
        XND_CKPT_PENDING,
        XND_SUSPINPROG,
        XND_CKPTINPROG,
        XND_ATFORK,
        XND_EXITING
};

enum xnd_state get_xnd_state(void);
void set_xnd_state(enum xnd_state);

void xnd_precheckpoint(void);
void xnd_postcheckpoint(void);
void xnd_postrestart_early(void);
void xnd_postrestart_late(void);
void xnd_checkpoint(ucontext_t *);

void xnd_atfork_prepare(void);
void xnd_atfork_child(void);
void xnd_atfork_parent(void);
void xnd_atfork_failed(void);
void xnd_register_fork_handlers(void);

#endif /* XND_LIB_H */
