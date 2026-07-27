/* readckpt.c */
#include "xnd/xnd.h"
#include "xnd/readckpt.h"
#include "xnd/xnd_lib.h"
#include "xnd/vm_region.h"
#include "xnd/pac.h"
#include "xnd/util/io.h"

#include <ucontext.h>
#include <unistd.h>

int read_vm_region(int fd, struct xnd_vm_region *region)
{
        ssize_t bytes;

        bytes = readall(fd, region, sizeof(*region));
        if (bytes != sizeof(*region)) {
                return -1;
        }

        if (ckpt_vm_restore_region(fd, region) < 0) {
                return -1;
        }
        
        return 0;
}

int read_context(int fd, ucontext_t *uctx)
{
        ssize_t bytes;
        
        bytes = readall(fd, uctx, sizeof(ucontext_t));
        if (bytes != sizeof(ucontext_t)) {
                return -1;
        }

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
        ssize_t                 bytes;

        for (u32 i = 0; i < header->entry_count; i++) {
                bytes = readall(fd, &entries[i], sizeof(entries[i]));
                if (bytes != sizeof(entries[i])) {
                        xnd_error("Failed to read checkpoint entry!\n");
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
                        xnd_error("Unrecognized checkpoint entry\n");
                        goto bad;
                }

                if (retval < 0) {
                        xnd_error("Failed to read checkpoint data\n");
                        goto bad;
                }

        }
        
        xnd_trace("Finished reading checkpoint (entries: %u, regions: %u)\n",
                  header->entry_count, header->region_count);
        close(fd);
        return 0;
bad:
        close(fd);
        return -1;
}
