/* ckptfile.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/path.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

        if (shared_cache_check(&header->shared_cache_info) < 0)
                return false;
        
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
                fprintf(stderr, "Failed to parse basename from "
                        "path to checkpoint file: %s!\n", path);
                return -1;
        } else if (xnd_path_stem(base, stem, sizeof(stem)) < 0) {
                fprintf(stderr, "Failed to parse stem of "
                        "checkpoint file: %s!\n", base);
                return -1;
        } else if (xnd_path_ext(base, ext, sizeof(ext)) < 0) {
                fprintf(stderr, "Failed to parse extension of "
                        "checkpoint file: %s!\n", base);
                return -1;
        }

        if (strcmp(ext, "ckpt")) {
                fprintf(stderr, "Not a valid checkpoint file: %s!\n", base);
                return -1;
        }
        
        delim = strrchr(stem, '-');
        if (!delim) {
                fprintf(stderr, "Not a valid checkpoint file: %s!\n", base);
                return -1;
        }
        
        if (timestamp)
                *timestamp = strtoull(delim + 1, NULL, 10);

        len = delim - stem;
        if (buflen < len + 1) {
                fprintf(stderr, "Buffer is too small, "
                        "need %zu bytes\n", len + 1);
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
