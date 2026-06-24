/* writeckpt.c */
#define _XOPEN_SOURCE
#include "xnd/xnd.h"
#include "xnd/writeckpt.h"
#include "xnd/ckptfile.h"
#include "xnd/vm_region.h"
#include "xnd/util/io.h"

#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

extern u64      epoch;
extern u32      xnd_pid;

int write_vm_region(int fd, const struct xnd_vm_region *region)
{
        ssize_t bytes;

        bytes = writeall(fd, region, sizeof(*region));
        if (bytes != sizeof(*region)) {
                return -1;
        }
        
        /**
         * Write vm region contents. If the region has no protection
         * bits, temporarily grant read permission in order to save
         * the contents of the region to the checkpoint file.
         */
        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, 0, VM_PROT_READ) < 0) {
                return -1;
        }
        
        bytes = writeall(fd, region->start, region->size);
        if (bytes != region->size) {
                return -1;
        }
        
        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, 0, VM_PROT_NONE) < 0) {
                return -1;
        }

        return 0;
}

int write_context(int fd, const ucontext_t *ctx)
{
        ssize_t bytes;
        
        bytes = writeall(fd, ctx, sizeof(*ctx));
        if (bytes != sizeof(*ctx)) {
                return -1;
        }

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
        ssize_t                         bytes;
        
        xnd_ckptfile_name(ckpt_out, sizeof(ckpt_out), 
                          header->xnd_uuid, epoch, xnd_pid);

        fd = open(ckpt_out, O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd < 0) {
                xnd_error("open: %s\n", strerror(errno));
                return -1;
        }
        
        bytes = writeall(fd, header, sizeof(*header));
        if (bytes != sizeof(*header)) {
                xnd_error("Failed to write checkpoint header\n");
                goto bad;
        }

        for (u32 i = 0; i < header->entry_count; i++) {
                bytes = writeall(fd, &entries[i], sizeof(entries[i]));
                if (bytes != sizeof(entries[i])) {
                        goto bad;
                }

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
