/* ckptfile.c */
#include "xnd/xnd.h"
#include "xnd/util/path.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

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
