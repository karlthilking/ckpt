/* writeckpt.c */
#define _XOPEN_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <errno.h>
#include "ckpt.h"
#include "pac.h"
#include "vm_region.h"
#include "writeckpt.h"

int ckptfilename(char *buf, size_t len)
{
        char *prog;
        
        prog = getenv("XND_PROGRAM");
        if (!prog) {
                perror("getenv");
                prog = "tmp";
        }
        
        snprintf(buf, len, "%s-%ld.ckpt", prog, (long)time(NULL));
        return 0;
}

int writeall(int fd, const void *buf, size_t size)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < size; bytes += retval) {
                if ((retval = write(fd, buf + bytes, size - bytes)) < 0) {
                        perror("write");
                        break;
                }
        }

        return (bytes == size) ? 0 : -1;
}

int write_vm_region(int fd, const ckpt_vm_region_t *rgn)
{
        /* Write vm region information (range, protections, etc) */
        if (writeall(fd, (void *)rgn, sizeof(*rgn)) < 0)
                return -1;
        
        /**
         * Write vm region contents. If the region has no protection
         * bits, temporarily grant read permission in order to save
         * the contents of the region to the checkpoint file.
         */
        if (rgn->prot == VM_PROT_NONE &&
            ckpt_vm_protect(rgn, 0, VM_PROT_READ) < 0)
                return -1;
        
        if (writeall(fd, rgn->start, rgn->size) < 0)
                return -1;
        
        if (rgn->prot == VM_PROT_NONE &&
            ckpt_vm_protect(rgn, 0, VM_PROT_NONE) < 0)
                return -1;

        return 0;
}

int write_context(int fd, const ckpt_context_t *ctx)
{
        if (writeall(fd, (void *)ctx, sizeof(*ctx)) < 0)
                return -1;

        return 0;
}

int write_ckpt(const ckpt_metadata_t *meta, 
               const ckpt_header_t *headers, 
               const ckpt_vm_region_t *regions, 
               const ckpt_context_t *contexts)
{
        int                     fd, retval;
        char                    ckptfile[256];
        const ckpt_vm_region_t  *rgn    = regions;
        const ckpt_context_t    *ctx    = contexts;

        ckptfilename(ckptfile, sizeof(ckptfile));
        fd = open(ckptfile, O_CREAT | O_EXCL | O_WRONLY, 0666);
        
        if (fd < 0) {
                perror("open");
                return -1;
        }

        /* Write checkpoint metadata to beginning of file */
        if (writeall(fd, meta, sizeof(*meta)) < 0) {
                goto bad;
        }

        for (u32 i = 0; i < meta->nr_headers; i++) {
                retval = writeall(fd, &headers[i], sizeof(headers[i]));
                if (retval != 0) {
                        goto bad;
                }

                switch (headers[i]) {
                case CKPT_VM_REGION_HEADER:
                        retval = write_vm_region(fd, rgn);
                        rgn++;
                        break;
                case CKPT_CONTEXT_HEADER:
                        retval = writeall(fd, ctx, sizeof(*ctx));
                        ctx++;
                        break;
                default:
                        /* Unrecognized header */
                        __builtin_trap();
                }

                if (retval < 0) {
                        goto bad;
                }
        }
        
        fprintf(stderr, "Wrote checkpoint to %s\n", ckptfile);
        close(fd);
        return 0;
bad:
        fprintf(stderr, "Failed to write checkpoint (%s)\n", ckptfile);
        close(fd);
        return -1;
}
