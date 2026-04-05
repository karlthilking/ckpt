/* restart.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <err.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "../include/ckpt.h"

void resign_frames(callframe_t *frames, int nr_frames, u64 *fp)
{
        u64 prev_fp, *prev_lr, prev_sp;

        for (int ix = 0; ix < nr_frames; ix++) {
                prev_fp = fp[0];
                while (prev_fp != frames[ix].fp) {
                        fp = (u64 *)prev_fp;
                        assert((u64)fp != 0);
                        prev_fp = fp[0];
                }

                prev_lr = fp + 1;
                prev_sp = (u64)(fp + 2);

                assert(*prev_lr == frames[ix].lr);
                assert(FR_SIGNED(frames[ix]));
                assert(FR_KEY(frames[ix]) == PAC_IBKEY);
                assert(FR_MOD(frames[ix]) == SP);
                PACIB(*prev_lr, prev_sp);
                
                if (prev_fp == 0 || prev_fp <= (u64)fp) {
                        assert(ix == nr_frames - 1);
                        break;
                }
                fp = (u64 *)prev_fp;
        }
}

void resign_regs(reg_ctx_t *ctx)
{
        mcontext_t mc = ctx->uc.uc_mcontext;

        if (ctx->pac_bitmap & (1ULL << LR) &&
            ctx->modifiers[LR] == SP) {
                PACIB(mc->__ss.__lr, mc->__ss.__sp);
        } else {
                fprintf(stderr,
                        "Link register (lr=%llu) was not signed using "
                        "the stack pointer as a modifier (sp=%llu)\n",
                        mc->__ss.__lr, mc->__ss.__sp);
                abort();
        }
}

void restart(int fd, int depth)
{
        if (depth > 0)
                restart(fd, depth - 1);
        else {
                int             rc;
                ckpt_metadata_t meta;

                if (read(fd, &meta, sizeof(ckpt_metadata_t)) < 0)
                        err(EXIT_FAILURE, "read");

                ckpt_hdr_t      hdrs[meta.nr_hdrs];
                mem_rgn_t       rgns[meta.nr_rgns];
                reg_ctx_t       ctxs[meta.nr_ctxs];
                callframe_t     frames[meta.nr_frames];

                rc = read_ckpt(fd, meta.nr_hdrs, meta.nr_rgns,
                               meta.nr_ctxs, meta.nr_frames,
                               hdrs, rgns, ctxs, frames);
                if (rc < 0)
                        exit(EXIT_FAILURE);
                
                off_t  offset;
                size_t step = sizeof(ckpt_hdr_t) + 
                              sizeof(mem_rgn_t);

                if ((offset = lseek(fd, sizeof(ckpt_metadata_t), 
                                    SEEK_SET)) < 0) {
                        err(EXIT_FAILURE, "lseek");
                }

                for (int i = 0; i < meta.nr_rgns; i++) {
                        offset += step;
                        if (restore_mem_rgn(fd, offset, 
                                            rgns + i) < 0) {
                                exit(EXIT_FAILURE);
                        }
                        offset += rgns[i].size;
                }
                
                u64 *fp = (u64 *)ctxs[0].uc.uc_mcontext->__ss.__sp;
                resign_frames(frames, meta.nr_frames, fp);
                resign_regs(&ctxs[0]);

                if (setcontext(&ctxs[0].uc) < 0)
                        err(EXIT_FAILURE, "setcontext");
        }
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./restart [ckpt-file]\n");
                exit(EXIT_FAILURE);
        }

        int fd;
        if ((fd = open(argv[1], O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open");

        restart(fd, 1000);
}
