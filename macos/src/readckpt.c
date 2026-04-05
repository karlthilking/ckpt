/* readckpt.c */
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <libproc.h>
#include <dlfcn.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc = 0;

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

        /**
         * Map back in region with mcontext_t data from saved
         * register context so it can be accessed when reading the
         * saved context in the checkpoint file
         */
        int  pos = -1;
        mach_vm_address_t addr;
        addr = (mach_vm_address_t)ctxs[0].uc.uc_mcontext;
        
        for (int i = 0; i < nr_rgns; i++) {
                if (addr >= rgns[i].start &&
                    addr + sizeof(mcontext_t) < rgns[i].end) {
                        pos = i;
                        printf("mcontext_t is in memory region at "
                               "%llx-%llx\n",
                               rgns[i].start, rgns[i].end);
                        break;
                }
        }
        
        if (pos < 0) {
                fprintf(stderr,
                        "Failed to find memory region where "
                        "mcontext_t data was saved\n");
                return -1;
        }

        if (lseek(fd, sizeof(ckpt_metadata_t), SEEK_SET) < 0)
                err(EXIT_FAILURE, "lseek");
        
        size_t step = sizeof(ckpt_hdr_t) + sizeof(mem_rgn_t);
        for (int i = 0; i < pos; i++) {
                if (lseek(fd, step, SEEK_CUR) < 0)
                        err(EXIT_FAILURE, "lseek");
                if (lseek(fd, rgns[i].size, SEEK_CUR) < 0)
                        err(EXIT_FAILURE, "lseek");
        }

        off_t offset;
        if ((offset = lseek(fd, step, SEEK_CUR)) < 0)
                err(EXIT_FAILURE, "lseek");

        void *ret = mmap((void *)rgns[pos].start, 
                         (size_t)rgns[pos].size,
                         PROT_READ | PROT_WRITE,
                         MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
                         -1, 0);
        if (ret == MAP_FAILED)
                err(EXIT_FAILURE, "mmap");
        
        size_t  bytes, total = (size_t)rgns[pos].size;
        void    *start = (void *)rgns[pos].start;
        ssize_t rv;
        for (bytes = 0; rv != -1 && bytes < total; bytes += rv) {
                rv = pread(fd, 
                           start + bytes, 
                           total - bytes,
                           offset + bytes);
        }
        if (rv < 0)
                err(EXIT_FAILURE, "pread");

        return 0;
}

void print_mem_rgn(mem_rgn_t *rgn)
{
        char cur_rwx[3];
        char max_rwx[3];
        char sh_mode[6];

        cur_rwx[0] = (rgn->prot & VM_PROT_READ)         ? 'r' : '-';
        cur_rwx[1] = (rgn->prot & VM_PROT_WRITE)        ? 'w' : '-';
        cur_rwx[2] = (rgn->prot & VM_PROT_EXECUTE)      ? 'x' : '-';
        max_rwx[0] = (rgn->max_prot & VM_PROT_READ)     ? 'r' : '-';
        max_rwx[1] = (rgn->max_prot & VM_PROT_WRITE)    ? 'w' : '-';
        max_rwx[2] = (rgn->max_prot & VM_PROT_EXECUTE)  ? 'x' : '-';
        
        switch (rgn->mode) {
        case SM_COW:
                strncpy(sh_mode, "COW", 4);
                break;
        case SM_EMPTY:
                strncpy(sh_mode, "EMPTY", 6);
                break;
        case SM_PRIVATE:
                strncpy(sh_mode, "PRV", 4);
                break;
        case SM_SHARED:
                strncpy(sh_mode, "SHR", 4);
                break;
        case SM_TRUESHARED:
                strncpy(sh_mode, "TRSHR", 6);
                break;
        case SM_SHARED_ALIASED:
                strncpy(sh_mode, "SHRAL", 6);
                break;
        case SM_PRIVATE_ALIASED:
                strncpy(sh_mode, "PRVAL", 6);
                break;
        case SM_LARGE_PAGE:
                strncpy(sh_mode, "LRGPG", 6);
                break;
        default:
                break;
        }

        printf("Memory region:\n");
        printf("%llu-%llu\t%zu\t%c%c%c/%c%c%c\tSM=%s\n",
               rgn->start, rgn->end, (size_t)rgn->size,
               cur_rwx[0], cur_rwx[1], cur_rwx[2],
               max_rwx[0], max_rwx[1], max_rwx[2],
               sh_mode);
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
        for (int i = 0; i < nr_rgns; i++) {
                print_mem_rgn(rgns + i);
        }
        for (int i = 0; i < nr_ctxs; i++)
                print_reg_ctx(ctxs + i);
        for (int i = 0; i < nr_frames; i++)
                print_callframe(frames + i);
}

void recursive(int fd, int levels)
{
        int rc;

        if (levels > 0)
                recursive(fd, levels - 1);
        else {
                ckpt_metadata_t meta;
        
                rc = read(fd, &meta, sizeof(ckpt_metadata_t));
                if (rc < 0)
                        err(EXIT_FAILURE, "read");

                ckpt_hdr_t      hdrs[meta.nr_hdrs];
                mem_rgn_t       rgns[meta.nr_rgns];
                reg_ctx_t       ctxs[meta.nr_ctxs];
                callframe_t     frames[meta.nr_frames];

                rc = read_ckpt(fd, meta.nr_hdrs, meta.nr_rgns,
                               meta.nr_ctxs, meta.nr_frames,
                               hdrs, rgns, ctxs, frames);
                if (rc < 0) {
                        fprintf(stderr,
                                "Failed to read checkpoint file\n");
                        exit(EXIT_FAILURE);
                }
                
                print_ckpt(meta.nr_rgns, meta.nr_ctxs, meta.nr_frames,
                           rgns, ctxs, frames);
        }
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./readckpt <ckpt-file>\n");
                exit(EXIT_FAILURE);
        }
        
        int fd;
        if ((fd = open(argv[1], O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open");
        
        recursive(fd, 1000);

        exit(0);
}
