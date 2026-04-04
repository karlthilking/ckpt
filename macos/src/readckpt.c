/* readckpt.c */
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc;

        for (bytes = 0; bytes < size && rc != -1; bytes += rc)
                rc = read(fd, data + bytes, size - bytes);

        if (rc < 0)
                perror("[Error] read");

        return (bytes == size) ? 0 : -1;
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

const char *mem_rgn_name(mem_rgn_t *r)
{
        if (COMMPAGE(r->start, r->size))
                return "[commpage]";
        else if (PAGEZERO(r->start, r->size))
                return "__PAGEZERO";
        else if (DYLD_SH_CH(r->start, r->size))
                return "[dyld shared cache]";
        
        switch (r->tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_NANO:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
                if (r->prot == VM_PROT_NONE)
                        return "[heap guard]";
                else if (r->tag == VM_MEMORY_MALLOC &&
                         r->prot == (VM_PROT_READ | VM_PROT_WRITE))
                        return "[heap metadata]";
                else if (r->prot == VM_PROT_READ)
                        return "[heap checksum]";
                return "[heap]";
        case VM_MEMORY_REALLOC:
                return "[kernel memory]";
        case VM_MEMORY_STACK:
                if (r->prot == VM_PROT_NONE)
                        return "[stack guard]";
                return "[stack]";
        case VM_MEMORY_GUARD:
                return "[guard]";
        case VM_MEMORY_DYLD:
                return "[dyld]";
        case VM_MEMORY_DYLD_MALLOC:
                return "[dyld heap]";
        case VM_MEMORY_DYLIB:
                return "[dylib]";
        case VM_MEMORY_SHARED_PMAP:
                return "[shared pmap]";
        default:
                break;
        }

        if (r->prot == VM_PROT_NONE)
                return "[guard]";
        else if (r->prot == (VM_PROT_READ | VM_PROT_EXECUTE))
                return "__TEXT";
        else if (r->prot == (VM_PROT_READ | VM_PROT_WRITE))
                return "__DATA";
        else if (r->prot == VM_PROT_READ)
                return "__DATA_CONST";
        
        return "[anonymous]";
}

void print_mem_rgn(mem_rgn_t *rgn)
{
        char rwxp[4];
        
        rwxp[0] = (rgn->prot & VM_PROT_READ)    ? 'r' : '-';
        rwxp[1] = (rgn->prot & VM_PROT_WRITE)   ? 'w' : '-';
        rwxp[2] = (rgn->prot & VM_PROT_EXECUTE) ? 'x' : '-';
        rwxp[3] = (rgn->mode == SM_SHARED)      ? 's' :
                  (rgn->mode == SM_PRIVATE)     ? 'p' : '-';
        
        printf("Memory region:\n");
        printf("%llx-%llx\t%zu\t%c%c%c%c\t%s\n",
               rgn->start, rgn->end, (size_t)rgn->size,
               rwxp[0], rwxp[1], rwxp[2], rwxp[3],
               mem_rgn_name(rgn));
}

void print_reg_ctx(reg_ctx_t *ctx)
{
        mcontext_t mc = ctx->uc.uc_mcontext;
        
        printf("Register context:\n");
        for (int i = 0; i < NGPREGS; i++)
                printf("x%d:\t%llu\n", i, mc->__ss.__x[i]);
        printf("fp:\t%llu\n", mc->__ss.__fp);
        printf("lr:\t%llu\n", mc->__ss.__lr);
        printf("sp:\t%llu\n", mc->__ss.__sp);
        printf("pc:\t%llu\n", mc->__ss.__pc);
}

void print_callframe(callframe_t *frame)
{
        printf("Frame record:\n");
        printf("fp:\t%llu\n", frame->fp);
        printf("lr:\t%llu\n", frame->lr);
        
        callframe_t cf = *frame;
        if (FR_SIGNED(cf) && (FR_MOD(cf) == SP))
                printf("lr was signed with sp as modifier\n");
}

void print_ckpt(int nr_rgns, int nr_ctxs, int nr_frames,
                mem_rgn_t *rgns, reg_ctx_t *ctxs,
                callframe_t *frames)
{
        for (int i = 0; i < nr_rgns; i++)
                print_mem_rgn(rgns + i);
        for (int i = 0; i < nr_ctxs; i++)
                print_reg_ctx(ctxs + i);
        for (int i = 0; i < nr_frames; i++)
                print_callframe(frames + i);
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./readckpt <ckpt-file>\n");
                exit(1);
        }
        
        int             fd, rc;
        ckpt_metadata_t meta;
        
        if ((fd = open(argv[1], O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open");
        
        if (read(fd, &meta, sizeof(ckpt_metadata_t)) < 0)
                err(EXIT_FAILURE, "read");
        
        ckpt_hdr_t      hdrs[meta.nr_hdrs];
        mem_rgn_t       rgns[meta.nr_rgns];
        reg_ctx_t       ctxs[meta.nr_ctxs];
        callframe_t     frames[meta.nr_frames];
        
        memset(hdrs, 0, sizeof(hdrs));
        rc = read_ckpt(fd, meta.nr_hdrs, meta.nr_rgns,
                       meta.nr_ctxs, meta.nr_frames,
                       hdrs, rgns, ctxs, frames);
        if (rc < 0) {
                fprintf(stderr, 
                        "[Error] Failed to read checkpoint file\n");
        }

        print_ckpt(meta.nr_rgns, meta.nr_ctxs, meta.nr_frames,
                   rgns, ctxs, frames);

        exit(0);
}
