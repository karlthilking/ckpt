/* readckpt.h */
#ifndef XND_READCKPT_H
#define XND_READCKPT_H

#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/ckptfile.h"

int readall(int, void *, size_t);
int read_vm_region(int, struct xnd_vm_region *);
int read_context(int, ucontext_t *);
int read_ckpt(int, const struct xnd_ckpt_header *, enum xnd_ckpt_entry *,
              struct xnd_vm_region *, ucontext_t *);

#endif /* XND_READCKPT_H */
