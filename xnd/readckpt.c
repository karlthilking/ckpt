/* readckpt.c */

#include "xnd/xnd.h"
#include "xnd/readckpt.h"
#include "xnd/ckpt.h"
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

int read_vm_region(int fd, ckpt_vm_region_t *rgn)
{
        int retval;

        retval = readall(fd, rgn, sizeof(*rgn));
        retval |= ckpt_vm_restore_region(fd, rgn);
        
        return retval;
}

int read_context(int fd, ucontext_t *uctx)
{
        if (readall(fd, uctx, sizeof(ucontext_t)) < 0)
                return -1;

        uctx->uc_mcontext = (mcontext_t)&uctx->__mcontext_data;
        return 0;
}

int read_ckpt(int fd, const ckpt_metadata_t *meta, ckpt_header_t *headers,
              ckpt_vm_region_t *regions, ucontext_t *uctx)
{
        ckpt_vm_region_t        *rgn = regions;
        int                     retval;

        for (u32 i = 0; i < meta->nr_headers; i++) {
                if (readall(fd, &headers[i], sizeof(headers[i])) < 0) {
                        xnd_error("Failed to read checkpoint header\n");
                        goto bad;
                }

                switch (headers[i]) {
                case CKPT_VM_REGION_HEADER:
                        retval = read_vm_region(fd, rgn);
                        rgn++;
                        break;
                case CKPT_CONTEXT_HEADER:
                        retval = read_context(fd, uctx);
                        break;
                default:
                        xnd_abort();
                }

                if (retval < 0) {
                        xnd_error("Failed to read checkpoint data!\n");
                        goto bad;
                }
        }

        close(fd);
        return 0;
bad:
        close(fd);
        return -1;
}
