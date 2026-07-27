/* writeckpt.h */
#ifndef XND_WRITECKPT_H
#define XND_WRITECKPT_H

#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/ckptfile.h"

#include <ucontext.h>

int write_vm_page(int, struct xnd_vm_region *, struct xnd_vm_page *);
int write_vm_region_pages(int, struct xnd_vm_region *);
int write_vm_region(int, struct xnd_vm_region *);
int write_context(int, ucontext_t *);
int write_ckpt(struct xnd_ckpt_header *, enum xnd_ckpt_entry *,
               struct xnd_vm_region *, ucontext_t *);

#endif /* XND_WRITECKPT_H */
