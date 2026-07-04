/* ckptfile.h */
#ifndef XND_CKPTFILE_H
#define XND_CKPTFILE_H

#include "xnd/xnd.h"
#include "xnd/shared_cache.h"
#include <mach/mach.h>
#include <sys/types.h>
#include <uuid/uuid.h>

#define XND_HEADER_MAGIC        "xnd_ckptfile_v0"

#define XND_CKPTPATH_FMT        "%s-checkpoints/epoch-%llu/ckpt-%u.xnd"
#define XND_CKPTPATH_MAXLEN     (XND_CKPTDIR_MAXLEN + XND_CKPTFILE_MAXLEN)

#define XND_CKPTFILE_FMT        "ckpt-%u.xnd"
#define XND_CKPTFILE_MAXLEN     (sizeof("ckpt-") + 10 + sizeof(".xnd") + 1)
#define XND_CKPTFILE_SUFFIX     "xnd"

#define XND_CKPTDIR_MAXLEN      (XND_CKPTDIR_BASELEN + XND_CKPTDIR_SUBLEN)
#define XND_CKPTDIR_BASELEN     (36 + sizeof("-checkpoints") + 1)
#define XND_CKPTDIR_SUBLEN      (sizeof("epoch-") + 21)

#define XND_MANIFEST_MAXLEN     (XND_CKPTDIR_MAXLEN + \
                                 sizeof(XND_MANIFEST_NAME))

#define XND_MANIFEST_NAME       "ckpt-manifest.xnd"
#define XND_MANIFEST_ENTRY_MAX  256

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
        pid_t                           pgid;
        pid_t                           sid;

        mach_port_t                     task_self;
        mach_port_t                     host_self;

        u32                             num_peers;
        u32                             is_root_of_tree;

        u32                             entry_count;
        u32                             region_count;
        struct shared_cache_info        shared_cache_info;
};

enum xnd_ckpt_entry {
        XND_VM_REGION_ENTRY,
        XND_VM_PAGE_ENTRY,
        XND_UCONTEXT_ENTRY
};

struct xnd_manifest {
        u32     entry_count;
        u32     xnd_pids[XND_MANIFEST_ENTRY_MAX];
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void xnd_ckptdir_name(char *, char *, const uuid_t, u64);
int xnd_ckptdir_create(uuid_t, u64);
int xnd_ckptdir_open(const uuid_t, u64);

int xnd_ckptfile_create(int, char *);
int xnd_ckptfile_openat(int, u32);
bool xnd_ckptfile_exists(int, u32);

int xnd_ckptfile_create_manifest(int);
int xnd_ckptfile_write_manifest(u32, u32, u32, const uuid_t, u64);
int xnd_ckptfile_extract_manifest(const char *, struct xnd_manifest *);

bool xnd_ckptfile_valid(const struct xnd_ckpt_header *);
int xnd_ckptfile_parse(const char *, uuid_t, u64 *, u32 *);
int xnd_ckptfile_name(char *, size_t, u32);
int xnd_ckptfile_extract_header(char *, struct xnd_ckpt_header *);
void xnd_ckptfile_write_header(struct xnd_ckpt_header *, 
                               u32, u32, uuid_t, u32, u32, u32, u32, bool);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_CKPTFILE_H */
