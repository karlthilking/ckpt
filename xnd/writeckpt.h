/* writeckpt.h */
#ifndef XND_WRITECKPT_H
#define XND_WRITECKPT_H

#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/ckptfile.h"

#define _XOPEN_SOURCE
#include <ucontext.h>

int writeall(int, const void *, size_t);
int write_vm_region(int, const struct xnd_vm_region *);
int write_context(int, const ucontext_t *);
int write_ckpt(const struct xnd_ckpt_header *,
               const enum xnd_ckpt_entry *,
               const struct xnd_vm_region *,
               const ucontext_t *);

#endif /* XND_WRITECKPT_H */
