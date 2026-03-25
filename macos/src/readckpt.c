#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <err.h>
#include "../include/ckpt.h"

int read_data(int fd, void *data, size_t size)
{
        ssize_t rc;
        size_t  bytes = 0;

        while (bytes < size) {
                rc = read(fd, data + bytes, size - bytes);
                if (rc < 0) {
                        perror("read");
                        return -1;
                }
                bytes += rc;
        }

        return bytes == size ? 0 : -1;
}

int shared_cache_rgn(mem_rgn_t *r)
{
        static u64 start = SHARED_REGION_BASE_ARM64;
        static u64 end   = SHARED_REGION_BASE_ARM64 +
                           SHARED_REGION_SIZE_ARM64;

        return (r->start >= start && r->end <= end);
}

void prot_str(vm_prot_t prot, u32 mode, char *rwxp)
{
        rwxp[0] = (prot & VM_PROT_READ) ? 'r' : '-';
        rwxp[1] = (prot & VM_PROT_WRITE) ? 'w' : '-';
        rwxp[2] = (prot & VM_PROT_EXECUTE) ? 'x' : '-';
        rwxp[3] = (prot & SM_PRIVATE) ? 'p' : 's';
}

const char *mem_rgn_name(mem_rgn_t *r)
{
        if (shared_cache_rgn(r))
                return "[dyld_shared_cache]";
        else if (r->start == 0 && r->prot == VM_PROT_NONE)
                return "[zeropage]";
        else if (r->prot == VM_PROT_NONE)
                return "[guard]";

        switch (r->tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_NANO:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
                return "[heap]";
        case VM_MEMORY_STACK:
                return "[stack]";
        case VM_MEMORY_GUARD:
                return "[guard]";
        case VM_MEMORY_SHARED_PMAP:
                return "[shared_pmap]";
        default:
                return "[anonymous]";
        }
}

void print_mem_rgn(mem_rgn_t *r)
{
        char            rwxp[4];
        const char      *name;

        prot_str(r->prot, r->mode, rwxp);
        name = mem_rgn_name(r);

        printf("%llx-%llx %c%c%c%c %s\n",
               r->start, r->end,
               rwxp[0], rwxp[1], rwxp[2], rwxp[3],
               name);
}

void print_reg_ctx(reg_ctx_t *c)
{
        mcontext_t mc = c->uc.uc_mcontext;

        for (int i = 0; i < 29; i++)
                printf("x%d:\t%llu\n", i, mc->__ss.__x[i]);

        printf("fp:\t%llu\n", mc->__ss.__fp);
        printf("lr:\t%llu\n", mc->__ss.__lr);
        printf("sp:\t%llu\n", mc->__ss.__sp);
        printf("pc:\t%llu\n", mc->__ss.__pc);
}

int read_ckpt(char *ckptfile)
{
        int             fd, rc, nr_hdrs = 0;
        size_t          bytes;
        off_t           ckpt_size;
        ckpt_hdr_t      hdrs[MAX_CKPT_HDRS];

        if ((fd = open(ckptfile, O_RDONLY)) < 0) {
                perror("open");
                return -1;
        }

        if ((ckpt_size = lseek(fd, 0, SEEK_END)) < 0) {
                perror("lseek");
                return -1;
        } else if (lseek(fd, 0, SEEK_SET) < 0) {
                perror("lseek");
                return -1;
        }

        while (bytes < ckpt_size) {
                rc = read_data(fd, hdrs + nr_hdrs,
                               sizeof(ckpt_hdr_t));
                
                if (rc < 0)
                        break;
                else if (hdrs[nr_hdrs].type & MEM_RGN_HDR) {
                        rc = lseek(fd, hdrs[nr_hdrs].rgn.size, 
                                   SEEK_CUR);
                        if (rc < 0) {
                                perror("lseek");
                                return -1;
                        }
                        bytes += hdrs[nr_hdrs].rgn.size;
                }

                bytes += sizeof(ckpt_hdr_t);
                nr_hdrs++;
        }

        for (int i = 0; i < nr_hdrs; i++) {
                if (hdrs[i].type & MEM_RGN_HDR)
                        print_mem_rgn(&hdrs[i].rgn);
                else if (hdrs[i].type & REG_CTX_HDR)
                        print_reg_ctx(&hdrs[i].ctx);
        }
        
        return 0;
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr,
                        "Usage: ./readckpt <ckpt-file>\n");
                exit(1);
        }

        read_ckpt(argv[1]);

        exit(0);
}
