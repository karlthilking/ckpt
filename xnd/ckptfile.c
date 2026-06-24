/* ckptfile.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/pid/pid.h"
#include "xnd/util/io.h"
#include "xnd/util/path.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

bool xnd_ckptfile_valid(const struct xnd_ckpt_header *header)
{
        if (strcmp(header->magic, XND_HEADER_MAGIC)) {
                xnd_error("Checkpoin header is invalid!\n"
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

int xnd_ckptfile_parse(const char *path, uuid_t uuid, u64 *epoch, u32 *xnd_pid)
{
        /**
         * Checkpoint filename format:
         *  <uuid>-<epoch>-<xnd_pid>.xnd
         */
        char    base[PATH_MAX], stem[PATH_MAX];
        char    uuid_str[37];
        char    ext[sizeof(XND_CKPTFILE_SUFFIX) + 1];
        int     err;

        err = xnd_path_basename(path, base, sizeof(base));
        if (err < 0) {
                xnd_error("Failed to parse basename of checkpoint path\n");
                return -1;
        }

        err = xnd_path_stem(base, stem, sizeof(stem));
        if (err < 0) {
                xnd_error("Failed to parse stem of checkpoint file\n");
                return -1;
        }

        err = xnd_path_ext(base, ext, sizeof(ext));
        if (err < 0) {
                xnd_error("Failed to parse extension of checkpoint file\n");
                return -1;
        }

        if (strcmp(ext, XND_CKPTFILE_SUFFIX)) {
                xnd_error("Invalid checkpoint file extension: %s\n", ext);
                return -1;
        }
        
        xnd_assert(strlen(stem) > sizeof(uuid_str));
        strncpy(uuid_str, stem, sizeof(uuid_str));
        uuid_parse(uuid_str, uuid);
        
        xnd_assert(sscanf(stem + 36, "%llu-%u", epoch, xnd_pid) == 2);
        return 0;
}

void xnd_ckptfile_name(char *out, size_t outlen, 
                       const uuid_t uuid, u64 epoch, u32 xnd_pid)
{
        /**
         * Checkpoint file format:
         *  <uuid>-<epoch>-<xnd_pid>.xnd
         */
        char uuid_str[37];

        uuid_unparse(uuid, uuid_str);
        snprintf(out, outlen, "%s-%llu-%u.xnd", uuid_str, epoch, xnd_pid);
}

int xnd_ckptfile_extract_header(char *path, struct xnd_ckpt_header *hdr)
{
        int     fd;
        ssize_t bytes;

        fd = open(path, O_RDONLY);
        if (fd < 0) {
                xnd_error("open: %s\n", strerror(errno));
                return -1;
        }

        xnd_assert(hdr != NULL);
        bytes = readall(fd, hdr, sizeof(struct xnd_ckpt_header));
        if (bytes != sizeof(struct xnd_ckpt_header)) {
                return -1;
        }

        if (strcmp(hdr->magic, XND_HEADER_MAGIC)) {
                xnd_error("Invalid checkpoint image: %s\n", path);
                return -1;
        }

        return 0;
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
        hdr->gid = _real_getgid();

        hdr->num_peers = num_peers;
        hdr->is_root_of_tree = is_root_of_tree;

        hdr->region_count = nr_regions;
        hdr->entry_count = nr_entries;
}
