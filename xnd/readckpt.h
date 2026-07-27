/* readckpt.h */
#ifndef XND_READCKPT_H
#define XND_READCKPT_H

#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/ckptfile.h"

#include <ucontext.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int read_vm_region(int, struct xnd_vm_region *);
int read_context(int, ucontext_t *);
int read_ckpt(int, const struct xnd_ckpt_header *, enum xnd_ckpt_entry *,
              struct xnd_vm_region *, ucontext_t *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_READCKPT_H */
