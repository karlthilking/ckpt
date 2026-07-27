/* xnd_setcontext.h */
#ifndef XND_UCONTEXT_H
#define XND_UCONTEXT_H

#include <ucontext.h>

extern void _xnd_setcontext(const void *);
void xnd_setcontext(ucontext_t *);

#endif /* XND_UCONTEXT_H */
