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

/**
 * xnd_ckptfile_parse:
 *  Parse the path to checkpoint file to extract the name of the program
 *  that was checkpointed and the timestamp of the checkpoint.
 * 
 *  Checkpoint file format: <program>-<timestamp>.ckpt
 * 
 *  @program: Out parameter filled with name of program from checkpoint
 *  @buflen: Size of buffer that should be filled with the program name
 *  @timestamp: Out parameter set to the timestamp of the checkpoint
 */
int xnd_ckptfile_parse(const char *path, char *program, 
                       size_t buflen, u64 *timestamp)
{
        char    *delim, base[PATH_MAX], stem[PATH_MAX], ext[5];
        size_t  len;

        if (xnd_path_basename(path, base, sizeof(base)) < 0) {
                xnd_error("Failed to parse basename from checkpoint "
                          "file path: %s\n", path);
                return -1;
        }

        if (xnd_path_stem(base, stem, sizeof(stem)) < 0) {
                xnd_error("Failed to parse stem of checkpoint "
                          "file: %s\n", base);
                return -1;
        }

        if (xnd_path_ext(base, ext, sizeof(ext)) < 0) {
                xnd_error("Failed to parse extension of checkpoint "
                          "file: %s\n", base);
                return -1;
        }

        if (strcmp(ext, XND_CKPTFILE_SUFFIX)) {
                xnd_error("Invalid checkpoint file: %s\n", base);
                return -1;
        }
        
        delim = strrchr(stem, '-');
        if (!delim) {
                xnd_error("Invalid checkpoint file: %s\n", base);
                return -1;
        }
        
        if (timestamp) {
                *timestamp = strtoull(delim + 1, NULL, 10);
        }

        len = delim - stem;
        if (buflen < len + 1) {
                xnd_error("Buffer is too small, need %zu bytes\n", len + 1);
                return -1;
        }

        strncpy(program, stem, len);
        program[len] = '\0';
        return 0;
}

void xnd_ckptfile_name(char *out, size_t outlen)
{
        char *prefix;
        
        prefix = getenv("XND_PROGRAM");
        xnd_assert(prefix != NULL);
        snprintf(out, outlen, "%s-%ld.ckpt", prefix, (long)time(NULL));
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
                               u32 xnd_pid, u32 xnd_ppid, u32 xnd_pgid,
                               u32 num_peers, bool is_root_of_tree)
{
        int err;

        bzero(hdr, sizeof(*hdr));
        strcpy(hdr->magic, XND_HEADER_MAGIC);
        
        err = shared_cache_get_info(&hdr->shared_cache_info);
        xnd_assert(err == 0);

        hdr->xnd_pid = xnd_pid;
        hdr->xnd_ppid = xnd_ppid;
        hdr->xnd_pgid = xnd_pgid;
        
        hdr->pid = _real_getpid();
        hdr->ppid = _real_getppid();
        hdr->sid = _real_getsid(0);
        hdr->gid = _real_getgid();

        hdr->num_peers = num_peers;
        hdr->is_root_of_tree = is_root_of_tree;

        /**
         * TODO:
         *  hdr->xnd_pid = ?
         *  hdr->xnd_ppid = ?
         *  hdr->xnd_group = ?
         *
         *  hdr->nr_peers = ?
         *  hdr->root_of_tree = ?
         */

        hdr->region_count = nr_regions;
        hdr->entry_count = nr_entries;
}
