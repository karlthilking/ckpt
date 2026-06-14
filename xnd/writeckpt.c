/* writeckpt.c */
#include "xnd/xnd.h"
#include "xnd/writeckpt.h"
#include "xnd/ckptfile.h"
#include "xnd/vm_region.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int writeall(int fd, const void *buf, size_t size)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < size; bytes += retval) {
                retval = write(fd, buf + bytes, size - bytes);
                if (retval < 0) {
                        xnd_error("write: %s\n", strerror(errno));
                        return -1;
                }
        }

        return 0;
}

int write_vm_region(int fd, const struct xnd_vm_region *region)
{
        if (writeall(fd, region, sizeof(struct xnd_vm_region)) < 0)
                return -1;
        
        /**
         * Write vm region contents. If the region has no protection
         * bits, temporarily grant read permission in order to save
         * the contents of the region to the checkpoint file.
         */
        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, 0, VM_PROT_READ) < 0)
                return -1;
        
        if (writeall(fd, region->start, region->size) < 0)
                return -1;
        
        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, 0, VM_PROT_NONE) < 0)
                return -1;

        return 0;
}

int write_context(int fd, const ucontext_t *ctx)
{
        if (writeall(fd, (void *)ctx, sizeof(ucontext_t)) < 0)
                return -1;

        return 0;
}

int write_ckpt(const struct xnd_ckpt_header *header, 
               const enum xnd_ckpt_entry *entries, 
               const struct xnd_vm_region *regions, 
               const ucontext_t *uctx)
{
        int                             fd, retval;
        char                            ckpt_out[256];
        const struct xnd_vm_region      *rgn = regions;
        
        xnd_ckptfile_name(ckpt_out, sizeof(ckpt_out));
        fd = open(ckpt_out, O_CREAT | O_EXCL | O_WRONLY, 0666);
        
        if (fd < 0) {
                xnd_error("open: %s\n", strerror(errno));
                return -1;
        }
        
        retval = writeall(fd, header, sizeof(struct xnd_ckpt_header));
        if (retval < 0) {
                xnd_error("Failed to write checkpoint header\n");
                goto bad;
        }

        for (u32 i = 0; i < header->entry_count; i++) {
                retval = writeall(fd, &entries[i], sizeof(entries[i]));
                if (retval != 0)
                        goto bad;

                switch (entries[i]) {
                case XND_VM_REGION_ENTRY:
                        retval = write_vm_region(fd, rgn);
                        rgn++;
                        break;
                case XND_UCONTEXT_ENTRY:
                        retval = write_context(fd, uctx);
                        break;
                default:
                        /* Unrecognized header */
                        xnd_abort();
                }

                if (retval < 0) {
                        xnd_error("Failed to write checkpoint data\n");
                        goto bad;
                }
        }
        
        printf("Checkpoint written to %s\n", ckpt_out);
        close(fd);
        return 0;
bad:
        xnd_error("Checkpoint failed (%s)\n", ckpt_out);
        close(fd);
        return -1;
}
