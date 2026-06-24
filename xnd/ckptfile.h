/* ckptfile.h */
#ifndef XND_CKPTFILE_H
#define XND_CKPTFILE_H

#include "xnd/xnd.h"
#include "xnd/shared_cache.h"
#include <sys/types.h>
#include <uuid/uuid.h>

#define XND_HEADER_MAGIC        "xnd_ckptfile_v0"
#define XND_CKPTFILE_SUFFIX     "xnd"
#define XND_CKPT_ENTRY_MAX      1024
#define XND_CKPT_VM_REGION_MAX  1023

struct xnd_ckpt_header {
        char                            magic[16];
        
        uuid_t                          xnd_uuid;
        u32                             xnd_pid;
        u32                             xnd_ppid;
        u32                             xnd_pgid;

        pid_t                           pid;
        pid_t                           ppid;
        pid_t                           sid;
        pid_t                           gid;

        u32                             num_peers;
        u32                             is_root_of_tree;

        u32                             entry_count;
        u32                             region_count;
        struct shared_cache_info        shared_cache_info;
};

enum xnd_ckpt_entry {
        XND_VM_REGION_ENTRY,
        XND_UCONTEXT_ENTRY
};

bool xnd_ckptfile_valid(const struct xnd_ckpt_header *);
int xnd_ckptfile_parse(const char *, uuid_t, u64 *, u32 *);
void xnd_ckptfile_name(char *, size_t, const uuid_t, u64, u32);
int xnd_ckptfile_extract_header(char *, struct xnd_ckpt_header *);
void xnd_ckptfile_write_header(struct xnd_ckpt_header *, 
                               u32, u32, uuid_t, u32, u32, u32, u32, bool);

#endif /* XND_CKPTFILE_H */
