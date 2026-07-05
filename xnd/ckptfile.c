/* ckptfile.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/pid/pid.h"
#include "xnd/util/io.h"
#include "xnd/util/path.h"
#include "xnd/util/compress.h"

#include <mach/mach.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <uuid/uuid.h>

extern mach_port_t task_self_trap(void);
extern mach_port_t host_self_trap(void);

void xnd_ckptpath_name(char *buf, uuid_t uuid, u64 epoch, 
                       u32 xnd_pid, bool use_zlib)
{
        char    uuid_str[37];
        size_t  len = XND_CKPTPATH_MAXLEN;
        
        bzero(buf, len);
        uuid_unparse(uuid, uuid_str);
        snprintf(buf, len, XND_CKPTPATH_FMT, uuid_str, epoch, xnd_pid);
        
        if (use_zlib) {
                strncat(buf, XND_COMPRESSED_SUFFIX, len - strlen(buf));
        }
}

void xnd_ckptdir_name(char *basedir, char *subdir, 
                      const uuid_t uuid, u64 epoch)
{
        uuid_unparse(uuid, basedir);
        strncat(basedir, "-checkpoints", sizeof("-checkpoints"));
        snprintf(subdir, XND_CKPTDIR_SUBLEN, "epoch-%llu", epoch);
}

int xnd_ckptdir_create(uuid_t uuid, u64 epoch)
{
        int     err, fd;
        char    base[XND_CKPTDIR_BASELEN];
        char    sub[XND_CKPTDIR_SUBLEN];

        xnd_ckptdir_name(base, sub, uuid, epoch);
        if (access(base, F_OK) != 0) {
                err = mkdir(base, 0755);
                if (err != 0) {
                        xnd_error("mkdir(%s): %s\n", base, strerror(errno));
                        return -1;
                }
        }

        fd = open(base, O_DIRECTORY | O_RDONLY);
        if (fd < 0) {
                xnd_error("open(%s): %s\n", base, strerror(errno));
                return -1;
        }

        err = mkdirat(fd, sub, 0755);
        if (err != 0) {
                xnd_error("mkdirat(%s): %s\n", sub, strerror(errno));
                close(fd);
                return -1;
        }
        
        close(fd);
        return 0;
}

int xnd_ckptdir_open(const uuid_t uuid, u64 epoch)
{
        int     err, fd;
        char    base[XND_CKPTDIR_BASELEN], sub[XND_CKPTDIR_SUBLEN];
        char    path[PATH_MAX];
        
        xnd_ckptdir_name(base, sub, uuid, epoch);
        err = xnd_path_join(path, sizeof(path), base, sub);
        if (err != 0) {
                xnd_error("xnd_path_join failed: %s + %s\n", base, sub);
                return -1;
        }

        fd = open(path, O_DIRECTORY | O_RDONLY);
        if (fd < 0) {
                xnd_error("open(%s): %s\n", path, strerror(errno));
                return -1;
        }

        return fd;
}

int xnd_ckptfile_create(int dirfd, char *ckptfile)
{
        int fd;

        fd = openat(dirfd, ckptfile, O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd < 0) {
                xnd_error("openat: %s\n", strerror(errno));
                return -1;
        }

        return fd;
}

int xnd_ckptfile_openat(int dirfd, u32 xnd_pid)
{
        int     fd;
        char    buf[XND_CKPTFILE_MAXLEN];

        snprintf(buf, sizeof(buf), XND_CKPTFILE_FMT, xnd_pid);
        fd = openat(dirfd, buf, O_RDONLY);
        if (fd < 0) {
                xnd_error("openat: %s\n", strerror(errno));
                return -1;
        }

        return fd;
}

bool xnd_ckptfile_exists(int dirfd, u32 xnd_pid)
{
        char buf[XND_CKPTFILE_MAXLEN];
        
        snprintf(buf, sizeof(buf), XND_CKPTFILE_FMT, xnd_pid);
        if (faccessat(dirfd, buf, F_OK, 0) == 0) {
                return true;
        }

        return false;
}

bool xnd_compressed_ckpt_exists(int dirfd, u32 xnd_pid)
{
        char buf[XND_CKPTFILE_MAXLEN];

        snprintf(buf, sizeof(buf), XND_COMPRESSED_CKPT_FMT, xnd_pid);
        if (faccessat(dirfd, buf, F_OK, 0) == 0) {
                return true;
        }

        return false;
}

int xnd_ckptfile_unlink(char *dir, u32 xnd_pid)
{
        int     dirfd, ret = 0;
        char    buf[XND_CKPTFILE_MAXLEN];

        dirfd = open(dir, O_DIRECTORY | O_RDONLY);
        if (dirfd < 0) {
                xnd_error("open(%s): %s\n", dir, strerror(errno));
                return -1;
        }
        
        snprintf(buf, sizeof(buf), XND_CKPTFILE_FMT, xnd_pid);
        if (unlinkat(dirfd, buf, 0) != 0) {
                xnd_warn("unlinkat: %s\n", strerror(errno));
                ret = -1;
        }
        
        close(dirfd);
        return ret;
}

int xnd_ckptfile_unlinkat(int dirfd, u32 xnd_pid)
{
        char buf[XND_CKPTFILE_MAXLEN];

        snprintf(buf, sizeof(buf), XND_CKPTFILE_FMT, xnd_pid);
        if (unlinkat(dirfd, buf, 0) != 0) {
                xnd_warn("unlinkat: %s\n", strerror(errno));
                return -1;
        }

        return 0;
}

int xnd_ckptfile_create_manifest(int dirfd)
{
        int fd, oflag;

        oflag = O_CREAT | O_EXCL | O_WRONLY;
        fd = openat(dirfd, XND_MANIFEST_NAME, oflag, 0666);
        if (fd < 0) {
                xnd_error("openat: %s\n", strerror(errno));
                return -1;
        }

        return fd;
}

int xnd_ckptfile_write_manifest(u32 total, u32 min_xnd_pid, u32 max_xnd_pid,
                                const uuid_t uuid, u64 epoch)
{
        struct xnd_manifest     manifest;
        int                     dirfd = -1, fd = -1;
        u32                     id, count;
        ssize_t                 bytes;
        
        dirfd = xnd_ckptdir_open(uuid, epoch);
        if (dirfd < 0) {
                xnd_error("Failed to open checkpoint directory\n");
                goto fail;
        }

        fd = xnd_ckptfile_create_manifest(dirfd);
        if (fd < 0) {
                xnd_error("Failed to create checkpoint manifest\n");
                goto fail;
        }
        
        count = 0;
        for (id = min_xnd_pid; id <= max_xnd_pid; id++) {
                if (xnd_ckptfile_exists(dirfd, id)) {
                        manifest.xnd_pids[count++] = id;
                } else if (xnd_compressed_ckpt_exists(dirfd, id)) {
                        manifest.xnd_pids[count++] = id;
                }
        }

        if (count != total) {
                xnd_error("Less checkpoint files than expected!\n");
                goto fail;
        }

        manifest.entry_count = count;
        bytes = writeall(fd, &manifest, sizeof(manifest));
        if (bytes != sizeof(manifest)) {
                xnd_error("Failed to write checkpoint manifest\n");
                goto fail;
        }
        
        close(dirfd);
        close(fd);
        return 0;
fail:
        if (dirfd != -1) {
                close(dirfd);
        }
        if (fd != -1) {
                close(fd);
        }
        return -1;
}

int xnd_ckptfile_extract_manifest(const char *path, 
                                  struct xnd_manifest *manifest)
{
        ssize_t bytes;
        int     fd = -1;
        
        fd = open(path, O_RDONLY);
        if (fd < 0) {
                xnd_error("open: %s\n", strerror(errno));
                goto fail;
        }

        bytes = readall(fd, manifest, sizeof(struct xnd_manifest));
        if (bytes != sizeof(struct xnd_manifest)) {
                goto fail;
        }

        close(fd);
        return 0;
fail:
        if (fd != -1) {
                close(fd);
        }
        return -1;
}


bool xnd_ckptfile_valid(const struct xnd_ckpt_header *header)
{
        if (strcmp(header->magic, XND_HEADER_MAGIC)) {
                xnd_error("Checkpoint header is invalid!\n"
                          "magic: %s, expected: %s\n",
                          header->magic, XND_HEADER_MAGIC);
                return false;
        }

        if (shared_cache_check(&header->shared_cache_info) < 0) {
                return false;
        }
        
        if (header->entry_count > XND_CKPT_ENTRY_MAX ||
            header->region_count > XND_CKPT_VM_REGION_MAX) {
                xnd_error("Checkpoint header is corrupted!\n");
                return false;
        }

        return true;
}

/**
 * Path is of the form:
 *  <uuid>-checkpoints/epoch-<epoch>/ckpt-<xnd_pid>.pid
 *
 * xnd_ckptfile_parse will extract each bit of metadata from the
 * path to a checkpoint file (computation uuid, checkpoint epoch, and xnd_pid
 * of the process that generated the given checkpoint file)
 */
int xnd_ckptfile_parse(const char *path, uuid_t uuid, u64 *epoch, u32 *xnd_pid)
{
        char    uuid_str[37];
        int     err;
        u64     ep;
        u32     id;

        strncpy(uuid_str, path, sizeof(uuid_str) - 1);
        uuid_str[sizeof(uuid_str) - 1] = '\0';

        err = uuid_parse(uuid_str, uuid);
        if (err != 0) {
                xnd_error("uuid_parse failed\n");
                return -1;
        }
        
        const char *str = path + 36 + sizeof("-checkpoints/");
        err = sscanf(str, "epoch-%llu/ckpt-%u.xnd", &ep, &id);
        if (err != 2) {
                xnd_error("sscanf: %s\n", strerror(errno));
                return -1;
        }

        *epoch = ep;
        *xnd_pid = id;
        return 0;
}

int xnd_ckptfile_name(char *out, size_t outlen, u32 xnd_pid)
{
        /**
         * Checkpoint file format:
         *  ckpt-<xnd_pid>.xnd
         */
        if (outlen < XND_CKPTFILE_MAXLEN) {
                xnd_error("Checkpoint file requires %zu bytes\n",
                          XND_CKPTFILE_MAXLEN);
                return -1;
        }

        snprintf(out, outlen, XND_CKPTFILE_FMT, xnd_pid);
        return 0;
}

int xnd_ckptfile_extract_header(char *path, struct xnd_ckpt_header *hdr)
{
        ssize_t bytes;
        int     fd = -1;

        if (fd < 0) {
                xnd_error("open: %s\n", strerror(errno));
                goto fail;
        }

        xnd_assert(hdr != NULL);
        bytes = readall(fd, hdr, sizeof(struct xnd_ckpt_header));
        if (bytes != sizeof(struct xnd_ckpt_header)) {
                goto fail;
        }

        if (strcmp(hdr->magic, XND_HEADER_MAGIC)) {
                xnd_error("Invalid checkpoint image: %s\n", path);
                goto fail;
        }
        
        close(fd);
        return 0;
fail:
        if (fd != -1) {
                close(fd);
        }
        return -1;
}

void xnd_ckptfile_write_header(struct xnd_ckpt_header *hdr, 
                               u32 nr_regions, u32 nr_entries,
                               uuid_t xnd_uuid, u32 xnd_pid, u32 xnd_ppid, 
                               u32 xnd_pgid, u32 num_peers, 
                               bool is_root_of_tree)
{
        bzero(hdr, sizeof(*hdr));
        strcpy(hdr->magic, XND_HEADER_MAGIC);
        
        xnd_assert(shared_cache_get_info(&hdr->shared_cache_info) == 0);
        
        memcpy(hdr->xnd_uuid, xnd_uuid, sizeof(uuid_t));
        hdr->xnd_pid = xnd_pid;
        hdr->xnd_ppid = xnd_ppid;
        hdr->xnd_pgid = xnd_pgid;
        
        hdr->pid = _real_getpid();
        hdr->ppid = _real_getppid();
        hdr->sid = _real_getsid(0);
        hdr->pgid = _real_getpgid(0);

        hdr->task_self = task_self_trap();
        hdr->host_self = host_self_trap();

        hdr->num_peers = num_peers;
        hdr->is_root_of_tree = is_root_of_tree;

        hdr->region_count = nr_regions;
        hdr->entry_count = nr_entries;
}
