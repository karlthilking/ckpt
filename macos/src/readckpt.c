/* readckpt.c */
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc = 0;

        for (bytes = 0; bytes < size && rc != -1; bytes += rc)
                rc = read(fd, data + bytes, size - bytes);

        if (rc < 0 && bytes != size) {
                perror("read");
                return -1;
        }

        return 0;
}

int read_ckpt(int fd, int nr_hdrs, int nr_rgns, 
              int nr_ctxs, int nr_frames,
              ckpt_hdr_t *hdrs, mem_rgn_t *rgns, 
              reg_ctx_t *ctxs, callframe_t *frames)
{
        assert(nr_hdrs == nr_rgns + nr_ctxs + nr_frames);
        
        int     rc, rd_hdrs, rd_rgns, rd_ctxs, rd_frames;
        void    *data   = NULL;
        size_t  size    = 0;

        rd_hdrs = rd_rgns = rd_ctxs = rd_frames = 0;
        for (int i = 0; i < nr_hdrs; i++) {
                data = (void *)(hdrs + i);
                size = sizeof(ckpt_hdr_t);
                if (read_data(fd, data, size) < 0) {
                        fprintf(stderr,
                                "[Error] Failed to read "
                                "checkpoint header");
                        return -1;
                }
                rd_hdrs++;

                switch (hdrs[i]) {
                case MEM_RGN_DATA:
                        data = (void *)(rgns + rd_rgns);
                        size = sizeof(mem_rgn_t);
                        if (read_data(fd, data, size) < 0)
                                return -1;
                        /* Skip actual memory region contents */
                        rc = lseek(fd, (off_t)rgns[rd_rgns].size,
                                   SEEK_CUR);
                        if (rc < 0) {
                                perror("lseek");
                                return -1;
                        }
                        rd_rgns++;
                        break;
                case REG_CTX_DATA:
                        data = (void *)(ctxs + rd_ctxs);
                        size = sizeof(reg_ctx_t);
                        if (read_data(fd, data, size) < 0)
                                return -1;
                        rd_ctxs++;
                        break;
                case CALLFRAME_DATA:
                        data = (void *)(frames + rd_frames);
                        size = sizeof(callframe_t);
                        if (read_data(fd, data, size) < 0)
                                return -1;
                        rd_frames++;
                        break;
                default:
                        assert(0 && "Unreachable\n");
                }
        }

        assert((rd_hdrs == nr_hdrs) && (rd_rgns == nr_rgns) &&
               (rd_ctxs == nr_ctxs) && (rd_frames == nr_frames)); 
        return 0;
}
