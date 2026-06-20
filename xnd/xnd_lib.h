/* ckpt.h */
#ifndef XND_LIB_H
#define XND_LIB_H

#include "xnd/xnd.h"
#include <sys/types.h>

enum xnd_state {
        XND_UNINITIALIZED,
        XND_RUNNING,
        XND_SUSPINPROG,
        XND_CKPTINPROG,
        XND_EXITING
};

enum xnd_state get_xnd_state(void);
void set_xnd_state(enum xnd_state);

void xnd_precheckpoint(void);
void xnd_postrestart(void);
void xnd_checkpoint(ucontext_t *);

void xnd_atfork_prepare(void);
void xnd_atfork_child(pid_t, pid_t);
void xnd_atfork_parent(pid_t, pid_t);
void xnd_atfork_failed(void);

void xnd_atfork_prepare(void);
void xnd_atfork_child(void);
void xnd_atfork_parent(pid_t);
void xnd_atfork_failed(void);

void xnd_setup(void);
void xnd_cleanup(void);

#endif /* XND_LIB_H */
