/* writeckpt.h */
#ifndef XND_WRITECKPT_H
#define XND_WRITECKPT_H

#include "xnd/xnd.h"

#define _XOPEN_SOURCE
#include <ucontext.h>

int writeall(int, const void *, size_t);
int write_vm_region(int, const ckpt_vm_region_t *);
int write_context(int, const ucontext_t *);
int write_ckpt(const ckpt_metadata_t *, const ckpt_header_t *, 
               const ckpt_vm_region_t *, const ucontext_t *);

#endif /* XND_WRITECKPT_H */
