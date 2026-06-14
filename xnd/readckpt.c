/* readckpt.c */
#include "xnd/xnd.h"
#include "xnd/readckpt.h"
#include "xnd/xnd_lib.h"
#include "xnd/vm_region.h"
#include "xnd/pac.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <unistd.h>

int readall(int fd, void *buf, size_t size)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < size; bytes += retval) {
                if ((retval = read(fd, buf + bytes, size - bytes)) < 0) {
                        perror("read");
                        break;
                }
        }

        return (bytes == size) ? 0 : -1;
}

int read_vm_region(int fd, struct xnd_vm_region *region)
{
        if (readall(fd, region, sizeof(struct xnd_vm_region)) < 0)
                return -1;
        else if (ckpt_vm_restore_region(fd, region) < 0)
                return -1;
        
        return 0;
}

int read_context(int fd, ucontext_t *uctx)
{
        if (readall(fd, uctx, sizeof(ucontext_t)) < 0)
                return -1;

        uctx->uc_mcontext = (mcontext_t)&uctx->__mcontext_data;
        return 0;
}

int read_ckpt(int fd, const struct xnd_ckpt_header *header, 
              enum xnd_ckpt_entry *entries,
              struct xnd_vm_region *regions, 
              ucontext_t *uctx)
{
        struct xnd_vm_region    *rgn = regions;
        int                     retval;

        for (u32 i = 0; i < header->entry_count; i++) {
                if (readall(fd, &entries[i], sizeof(entries[i])) < 0) {
                        xnd_error("Failed to read checkpoint entry\n");
                        goto bad;
                }

                switch (entries[i]) {
                case XND_VM_REGION_ENTRY:
                        retval = read_vm_region(fd, rgn);
                        rgn++;
                        break;
                case XND_UCONTEXT_ENTRY:
                        retval = read_context(fd, uctx);
                        break;
                default:
                        xnd_abort();
                }

                if (retval < 0) {
                        xnd_error("Failed to read checkpoint data\n");
                        goto bad;
                }
        }

        close(fd);
        return 0;
bad:
        close(fd);
        return -1;
}
