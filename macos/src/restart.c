/* restart.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc;

        for (bytes = 0, rc = 0; rc != -1 && bytes < size; bytes += rc)
                rc = read(fd, data + bytes, size - bytes);

        if (rc < 0) {
                perror("read");
                return -1;
        }

        return 0;
}

int restore_mem_rgn(int fd, mem_rgn_t *r)
{
        void    *addr   = (void *)r->start;
        size_t  len     = (size_t)r->size;
        int     prot = 0, flags = 0;
        
        prot |= (r->prot & VM_PROT_READ)     ? PROT_READ  : PROT_NONE;
        prot |= (r->prot & VM_PROT_WRITE)    ? PROT_WRITE : PROT_NONE;
        prot |= (r->prot & VM_PROT_EXECUTE)  ? PROT_EXEC  : PROT_NONE; 

        /* Pages can not be writable and executable */
        assert(!((prot & PROT_WRITE) && (prot & PROT_EXEC)));

        addr = mmap(addr,
                    len,
                    PROT_READ | PROT_WRITE,
                    MAP_ANON | MAP_PRIVATE | MAP_FIXED,
                    -1, 0);

        if (addr == MAP_FAILED) {
                fprintf(stderr,
                        "mmap (%llx-%llx): %s\n",
                        (u64)r->start, (u64)r->end, strerror(errno));
                return -1;
        } else if (addr != (void *)r->start) {
                fprintf(stderr,
                        "mmap(...,MAP_FIXED,...) could not place saved "
                        "memory region at its original address.\n"
                        "Desired: %llu\nActual: %llu\n",
                        (u64)r->start, (u64)addr);
                return -1;
        }

        if (read_data(fd, addr, (size_t)r->size) < 0)
                return -1;
        
        if (prot & PROT_EXEC) {
                if (mprotect(addr, len, PROT_READ) < 0) {
                        perror("mprotect");
                        return -1;
                } else if (mprotect(addr, len, prot) < 0) {
                        perror("mprotect");
                        return -1;
                }
        } else if (mprotect(addr, len, prot) < 0) {
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

        int     rd_hdrs, rd_rgns, rd_ctxs, rd_frames;
        void    *data;
        
        rd_hdrs = rd_rgns = rd_ctxs = rd_frames = 0;
        for (int i = 0; i < nr_hdrs; i++) {
                data = (void *)(hdrs + i);
                if (read_data(fd, data, sizeof(ckpt_hdr_t)) < 0)
                        return -1;
                rd_hdrs++;

                switch (hdrs[i]) {
                case MEM_RGN_DATA:
                        data = (void *)(rgns + rd_rgns);
                        if (read_data(fd, data, sizeof(mem_rgn_t)) < 0)
                                return -1;
                        if (restore_mem_rgn(fd, rgns + rd_rgns) < 0)
                                return -1;
                        rd_rgns++;
                        break;
                case REG_CTX_DATA:
                        data = (void *)(ctxs + rd_ctxs);
                        if (read_data(fd, data, sizeof(reg_ctx_t)) < 0)
                                return -1;
                        ctxs[rd_ctxs].uc.uc_mcontext = &ctxs[rd_ctxs].mc;
                        rd_ctxs++;
                        break;
                case CALLFRAME_DATA:
                        data = (void *)(frames + rd_frames);
                        if (read_data(fd, data, sizeof(callframe_t)) < 0)
                                return -1;
                        rd_frames++;
                        break;
                default:
                        assert(0 && "Unreachable branch\n");
                }
        }

        assert(rd_hdrs == nr_hdrs && rd_rgns == nr_rgns &&
               rd_ctxs == nr_ctxs && rd_frames == nr_frames);
        return 0;
}

void resign_frames(callframe_t *frames, int nr_frames, u64 *fp)
{
        u64 prev_fp, *prev_lr, prev_sp;
        
        for (int i = 0; i < nr_frames; i++) {
                prev_fp = fp[0];
                while (prev_fp != frames[i].fp) {
                        fp = (u64 *)prev_fp;
                        assert((u64)fp != 0);
                        prev_fp = fp[0];
                }
                
                prev_lr = fp + 1;
                prev_sp = (u64)(fp + 2);

                assert(*prev_lr == frames[i].lr);
                assert(FR_SIGNED(frames[i]));
                assert(FR_KEY(frames[i]) == PAC_IBKEY);
                assert(FR_MOD(frames[i]) == SP);
                
                /* Resign link register with restart process's IB Key */
                PACIB(*prev_lr, prev_sp);
                
                u64 lr = *prev_lr;
                u64 sp = prev_sp;
                AUTIB(lr, sp);
                assert(!AUTHFAIL(lr));

                fp = (u64 *)prev_fp;
        }
}

void resign_regs(reg_ctx_t *ctx)
{
        mcontext_t mc = &ctx->mc;

        if (ctx->pac_bitmap & (1ULL << LR)) {
                if (ctx->modifiers[LR] != SP) {
                        fprintf(stderr,
                                "Unexpected link register signature."
                                "The link register (lr=%llu) was not "
                                "signed using the stack pointer as "
                                "the modifier (sp=%llu)\n",
                                mc->__ss.__lr, mc->__ss.__sp);
                        abort();
                }
                PACIB(mc->__ss.__lr, mc->__ss.__sp);
                /**
                 * Make sure re-signing should not cause an 
                 * authentication failure later on
                 */
                u64 lr = mc->__ss.__lr;
                AUTIB(lr, mc->__ss.__sp);
                assert(!AUTHFAIL(lr));
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
                
                assert(ctxs[0].uc.uc_mcontext == &ctxs[0].mc);

                u64 *fp = (u64 *)ctxs[0].mc.__ss.__fp;
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
        
        printf("Restarting from %s (pid=%d)\n",
               argv[1], getpid());
        raise(SIGSTOP);

        restart(fd, 2048);
}
