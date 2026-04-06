/* printckpt.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc = 0;

        for (bytes = 0; rc != -1 && bytes < size; bytes += rc)
                rc = read(fd, data + bytes, size - bytes);

        if (rc < 0) {
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
        void    *data;
        size_t  size;

        rd_hdrs = rd_rgns = rd_ctxs = rd_frames = 0;
        for (int i = 0; i < nr_hdrs; i++) {
                data = (void *)(hdrs + i);
                size = sizeof(ckpt_hdr_t);
                if (read_data(fd, data, size) < 0)
                        return -1;
                rd_hdrs++;

                switch (hdrs[i]) {
                case MEM_RGN_DATA:
                        data = (void *)(rgns + rd_rgns);
                        size = sizeof(mem_rgn_t);
                        if (read_data(fd, data, size) < 0)
                                return -1;
                        rc = lseek(fd, rgns[rd_rgns].size, SEEK_CUR);
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
                        assert(0 && "Unreachable branch\n");
                }
        }

        assert((rd_hdrs == nr_hdrs) && (rd_rgns == nr_rgns) &&
               (rd_ctxs == nr_ctxs) && (rd_frames == nr_frames));
        return 0;
}

int restore_mem_rgn(int fd, off_t offset, mem_rgn_t *r)
{
        void    *addr   = (void *)r->start;
        size_t  len     = (size_t)r->size;

        addr = mmap(addr,
                    len,
                    PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                    -1, 0);
        if (addr == MAP_FAILED) {
                perror("mmap");
                return -1;
        } else if (addr != (void *)r->start) {
                fprintf(stderr,
                        "mmap(...,MAP_FIXED,...)\n"
                        "Desired: %llu\nActual: %llu\n",
                        (u64)r->start, (u64)addr);
                return -1;
        }
        
        if (lseek(fd, 0, SEEK_CUR) != offset &&
            lseek(fd, offset, SEEK_SET) < 0) {
                perror("lseek");
                return -1;
        }

        if (read_data(fd, addr, len) < 0)
                return -1;
        
        return 0;
}

void prot_str(vm_prot_t prot, char *rwx)
{
        rwx[0] = (prot & VM_PROT_READ)          ? 'r' : '-';
        rwx[1] = (prot & VM_PROT_WRITE)         ? 'w' : '-';
        rwx[2] = (prot & VM_PROT_EXECUTE)       ? 'x' : '-';
        rwx[3] = '\0';
}

void share_mode_str(int mode, char *mode_str)
{
        switch (mode) {
        case SM_PRIVATE:
                strncpy(mode_str, "PRV", strlen("PRV") + 1);
                return;
        case SM_COW:
                strncpy(mode_str, "COW", strlen("COW") + 1);
                return;
        case SM_SHARED:
        case SM_TRUESHARED:
                strncpy(mode_str, "SHM", strlen("SHM") + 1);
                return;
        case SM_EMPTY:
                strncpy(mode_str, "NUL", strlen("NUL") + 1);
                return;
        case SM_PRIVATE_ALIASED:
                strncpy(mode_str, "ALI", strlen("ALI") + 1);
                return;
        case SM_SHARED_ALIASED:
                strncpy(mode_str, "S/A", strlen("S/A") + 1);
                return;
        default:
                return;
        }
}

void print_mem_rgn(mem_rgn_t *r)
{
        char cur_prot[4];
        char max_prot[4];
        char share_mode[4];

        prot_str(r->prot, cur_prot);
        prot_str(r->prot, max_prot);
        share_mode_str(r->mode, share_mode);

        printf("%llx-%llx\t%zu\t%s/%s\tSM=%s\n",
               r->start, r->end, (size_t)r->size,
               cur_prot, max_prot, share_mode);
}

void print_reg_ctx(reg_ctx_t *ctx)
{
        mcontext_t mc = ctx->uc.uc_mcontext;

        for (int i = 0; i < NGPREGS; i++)
                printf("x%d:\t%llu\n", i, mc->__ss.__x[i]);
        
        printf("fp:\t%llu\n", mc->__ss.__fp);
        printf("lr:\t%llu\n", mc->__ss.__lr);
        printf("sp:\t%llu\n", mc->__ss.__sp);
        printf("pc:\t%llu\n", mc->__ss.__pc);
}

void print_callframe(callframe_t *cf)
{
        printf("fp:\t\t%llu\n", cf->fp);
        printf("lr:\t\t%llu\n", cf->lr);
        
        callframe_t f = *cf;
        if (FR_SIGNED(f) && (FR_MOD(f) == SP))
                printf("Key:\t\tIB Key\nModifier:\tSP\n");
}

void print_ckpt(int nr_rgns, int nr_ctxs, int nr_frames,
                mem_rgn_t *rgns, reg_ctx_t *ctxs, 
                callframe_t *frames)
{
        printf("Printing %d saved memory regions...\n", nr_rgns);
        for (int i = 0; i < nr_rgns; i++) {
                print_mem_rgn(rgns + i);
        }
        putchar('\n');
        
        printf("Printing saved register context...\n");
        for (int i = 0; i < nr_ctxs; i++) {
                print_reg_ctx(ctxs + i);
                putchar('\n');
        }
        
        printf("Printing saved frame records...\n");
        for (int i = 0; i < nr_frames; i++) {
                printf("Call Frame %d:\n", i + 1);
                print_callframe(frames + i);
                putchar('\n');
        }
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
                if (rc < 0)
                        exit(EXIT_FAILURE);

                /**
                 * Restore region where mcontext_t in ucontext_t is
                 * stored in order to print the register contents
                 * saved in the ucontext_t struct
                 */
                mach_vm_address_t       start, end;
                int                     pos = -1;
                off_t                   offset;
                size_t                  step;

                start = (mach_vm_address_t)ctxs[0].uc.uc_mcontext;
                end = start + sizeof(mcontext_t);
                
                if ((offset = lseek(fd, sizeof(ckpt_metadata_t),
                                    SEEK_SET)) < 0)
                        err(EXIT_FAILURE, "lseek");
        
                /**
                 * Move file offset to position of memory region where
                 * the mcontext_t data resides
                 */
                step = sizeof(ckpt_hdr_t) + sizeof(mem_rgn_t);
                for (int i = 0; i < meta.nr_rgns; i++) {
                        if (start >= rgns[i].start &&
                            end < rgns[i].end) {
                                offset += step;
                                pos = i;
                                break;
                        }
                        offset += (step + rgns[i].size);
                }
                assert(pos != -1);

                if (restore_mem_rgn(fd, offset, &rgns[pos]) < 0)
                        exit(EXIT_FAILURE);
                
                print_ckpt(meta.nr_rgns, 
                           meta.nr_ctxs,
                           meta.nr_frames,
                           rgns, ctxs, frames);
        }
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./readckpt [ckpt-file]\n");
                exit(EXIT_FAILURE);
        }

        int fd;
        if ((fd = open(argv[1], O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open");

        recursive(fd, 1024);
        exit(0);
}
