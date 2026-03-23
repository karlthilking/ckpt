#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ucontext.h>
#include <errno.h>
#include <err.h>
#include <sys/mman.h>
#include "testlib.h"

char *regs[NGREG] =
{
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
        "RDI", "RSI", "RBP", "RBX", "RDX", "RAX", "RCX", "RSP",
        "RIP", "EFL", "CSGSFS", "ERR", "TRAPNO", "OLDMASK", "CR2"
};

int rd_ckptdata(int fd, void *data, size_t sz)
{
        int rc, bytes = 0;

        while (bytes < sz) {
                rc = read(fd, data + bytes, sz - bytes);
                if (rc < 0) {
                        perror("read");
                        return -1;
                }
                bytes += rc;
        }

        assert(bytes == sz);
        return 0;
}

int print_ckptfile(char *ckptfile)
{
        int fd;
        int nr_ckpthdrs = 0, nr_memrgns = 0, nr_regctxs = 0;
        off_t ckpt_sz;

        ckpthdr_t       hdrs[MAX_CKPTHDRS];
        memrgn_t        rgns[MAX_MEMRGNS];
        regctx_t        ctxs[MAX_REGCTXS];

        if ((fd = open(ckptfile, O_RDONLY)) < 0) {
                perror("open");
                return -1;
        }
        
        if ((ckpt_sz = lseek(fd, 0, SEEK_END)) < 0) {
                perror("lseek");
                return -1;
        } else if (lseek(fd, 0, SEEK_SET) != 0) {
                perror("lseek");
                return -1;
        }
        
        /**
         * Format of checkpoint file:
         * header 0
         * regctx_t + ucontext_t or memrgn_t + memory segment
         * header 1
         * ...
         */
        void *data = NULL;
        size_t sz = 0;
        int bytes = 0;

        while (bytes < ckpt_sz) {
                ckpthdr_t *h = hdrs + nr_ckpthdrs;
                
                data = (void *)h;
                sz = sizeof(ckpthdr_t);

                if (rd_ckptdata(fd, data, sz) < 0)
                        return -1;
                bytes += sz;
                
                if (h->type & TY_REGCTX) {
                        regctx_t *c = h->ctx;

                        data = (void *)c;
                        sz = sizeof(regctx_t);

                        if (rd_ckptdata(fd, data, sz) < 0)
                                return -1;
                        bytes += sz;

                        ucontext_t *uc = c->uc;
                        sz = sizeof(ucontext_t);

                        if (rd_ckptdata(fd, data, sz) < 0)
                                return -1;
                        bytes += sz;
                        nr_regctxs++;
                } else if (h->type & TY_MEMRGN) {
                        memrgn_t *r = h->rgn;

                        data = (void *)r;
                        sz = sizeof(memrgn_t);

                        if (rd_ckptdata(fd, data, sz) < 0)
                                return -1;
                        bytes += sz;
                        
                        /* Skip past actual memory region */
                        sz = (u64)(r->end - r->start);
                        if (lseek(fd, sz, SEEK_CUR) < 0) {
                                perror("lseek");
                                return -1;
                        }
                        bytes += sz;
                        nr_memrgns++;
                }
                nr_ckpthdrs++;
        }

        printf("Displaying Memory Regions\n");
        for (int i = 0; i < nr_memrgns; i++) {
                printf("%lx-%lx %c%c%c%c %s\n",
                       (u64)rgns[i].start,
                       (u64)rgns[i].end,
                       (rgns[i].rwxp & R) ? 'r' : '-',
                       (rgns[i].rwxp & W) ? 'w' : '-',
                       (rgns[i].rwxp & X) ? 'x' : '-',
                       (rgns[i].rwxp & P) ? 'p' : 's',
                       rgns[i].name);
        }

        printf("Displaying Register contexts\n");
        for (int i = 0; i < nr_regctxs; i++) {
                for (int j = 0; j < NGREG; j++) {
                        printf("%-12s%p", 
                               regs[i],
                               ctxs[i].uc->uc_mcontext.gregs[i]);
                }
        }

        close(fd);
        return 0;
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./print <ckpt-file>\n");
                exit(1);
        }
       
        if (print_ckptfile(argv[1]) < 0)
                exit(1);

        exit(0);
}
