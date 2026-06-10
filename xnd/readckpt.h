/* readckpt.h */
#ifndef XND_READCKPT_H
#define XND_READCKPT_H

#include "xnd/xnd.h"

int readall(int, void *, size_t);
int read_vm_region(int, ckpt_vm_region_t *);
int read_context(int, ucontext_t *);
int read_ckpt(int, const ckpt_metadata_t *, ckpt_header_t *,
              ckpt_vm_region_t *, ucontext_t *);

#endif /* XND_READCKPT_H */
