/* ckptfile.h */
#ifndef XND_CKPTFILE_H
#define XND_CKPTFILE_H

#include "xnd/xnd.h"
#include "xnd/shared_cache.h"
#include <sys/types.h>

#define XND_HEADER_MAGIC        "xnd_ckptfile_v0"
#define XND_CKPT_ENTRY_MAX      1024
#define XND_CKPT_VM_REGION_MAX  1023

struct xnd_ckpt_header {
        char                            magic[16];
        pid_t                           pid;
        pid_t                           ppid;
        u32                             entry_count;
        u32                             region_count;
        struct shared_cache_info        shared_cache_info;
};

enum xnd_ckpt_entry {
        XND_VM_REGION_ENTRY,
        XND_UCONTEXT_ENTRY
};

bool xnd_ckptfile_valid(const struct xnd_ckpt_header *);
int xnd_ckptfile_parse(const char *, char *, size_t, u64 *);
void xnd_ckptfile_name(char *, size_t);

#endif /* XND_CKPTFILE_H */
