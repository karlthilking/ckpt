/* restart.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ucontext.h>
#include <assert.h>
#include <errno.h>
#include <sys/sysctl.h>
#include "../include/ckpt.h"

u32 n_va_bits()
{
        int     rc;
        size_t  size;
        u32     va_bits;

        rc = sysctlbyname("machdep.virtual_address_size",
                          &va_bits, &size, NULL, 0);

        if (rc != -1)
                return va_bits;

        rc = sysctlbyname("machdep.cpu.address_bits.virtual",
                          &va_bits, &size, NULL, 0);

        if (rc != -1)
                return va_bits;
        else {
                fprintf(stderr,
                        "The virtual address space size could "
                        "not be identified: %s\n",
                        strerror(errno));
                /**
                 * Assume a 48 bit virtual address space, could
                 * be inaccurate.
                 */
                return 48;
        }
}

size_t read_data(int fd, void *data, size_t size)
{
        ssize_t rc;
        size_t  bytes = 0;

        while (bytes < size) {
                rc = read(fd, data + bytes, size - bytes);
                if (rc < 0) {
                        perror("read");
                        return -1;
                } else if (rc == 0 || rc == EOF) {
                        return 0;
                }
                bytes += rc;
        }

        return bytes;
}

int restore_mem_rgn(int fd, ckpt_hdr_t *hdr)
{
        kern_return_t   kr;
        int             flags;
        vm_prot_t       prot;
        size_t          bytes;
        ssize_t         rc;

        flags = VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE;

        /* Map in saved virtual memory region */
        kr = mach_vm_allocate(mach_task_self(),
                              &hdr->rgn.start,
                              hdr->rgn.size,
                              flags);

        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "mach_vm_allocate: %s\n",
                        mach_error_string(kr));
                return -1;
        }
        
        /**
         * Grant full max permissions to ensure that write
         * permissions to this segment for restore do not
         * conflict with the max protection limit
         */
        prot = VM_PROT_READ | VM_PROT_WRITE;
        kr = mach_vm_protect(mach_task_self(),
                             hdr->rgn.start,
                             hdr->rgn.size,
                             1, prot);
        
        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "mach_vm_protect: %s\n",
                        mach_error_string(kr));
                return -1;
        }
        
        /**
         * Now set current memory region permission with
         * full permissions enabled
         */
        kr = mach_vm_protect(mach_task_self(),
                             hdr->rgn.start,
                             hdr->rgn.size,
                             0, prot);
        
        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "mach_vm_protect: %s\n",
                        mach_error_string(kr));
                return -1;
        }
        
        /**
         * Read saved data from checkpoint file into this
         * memory region to restore address space
         */
        void *addr = (void *)hdr->rgn.start;
        for (bytes = 0; bytes < hdr->rgn.size; bytes += rc) {
                if ((rc = read(fd, addr, hdr->rgn.size)) < 0)
                        return -1;
        }
        
        /**
         * Reset current and max protection for this region
         * to the original configuration
         */
        kr = mach_vm_protect(mach_task_self(),
                             hdr->rgn.start,
                             hdr->rgn.size,
                             0, hdr->rgn.prot);
        
        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "mach_vm_protect: %s\n",
                        mach_error_string(kr));
                return -1;
        }

        kr = mach_vm_protect(mach_task_self(),
                             hdr->rgn.start,
                             hdr->rgn.size,
                             1, hdr->rgn.max_prot);

        if (kr != KERN_SUCCESS) {
                fprintf(stderr,
                        "mach_vm_protect: %s\n",
                        mach_error_string(kr));
                return -1;
        }

        return 0;
}

int read_ckpt(char *ckptfile, ckpt_hdr_t *hdrs, int *nr_hdrs)
{
        int     fd, rc;
        off_t   ckpt_size;
        size_t  bytes  = 0; // bytes of checkpoint file read
        int     n_read = 0; // number of headers read

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
                rc = read_data(fd, hdrs + n_read,
                               sizeof(ckpt_hdr_t));
                if (rc <= 0)
                        break;

                bytes += sizeof(ckpt_hdr_t);

                if (hdrs[n_read].type == REG_CTX_HDR) {
                        n_read++;
                        break;
                } else if (hdrs[n_read].type == MEM_RGN_HDR) {
                        rc = restore_mem_rgn(fd, hdrs + n_read);
                        if (rc < 0)
                                return -1;
                        bytes += hdrs[n_read].rgn.size;
                }
                n_read++;
        }

        *nr_hdrs = n_read;
        return 0;
}

int restart(char *ckptfile)
{
        ckpt_hdr_t      hdrs[MAX_CKPT_HDRS];
        int             nr_hdrs = 0;
        
        memset(hdrs, 0, sizeof(ckpt_hdr_t) * MAX_CKPT_HDRS);
        if (read_ckpt(ckptfile, hdrs, &nr_hdrs) < 0)
                return -1;
        
        assert(nr_hdrs > 0);
        
        /**
         * Before committing to the restart, make sure that
         * no restored registers are signed 
         */
        mcontext_t mc = hdrs[nr_hdrs - 1].ctx.uc.uc_mcontext;
        u64 va_mask   = (UINT64_C(1) << n_va_bits()); 

        for (int i = 0; i < 29; i++)
                assert(!PACSIGNED(mc->__ss.__x[i], va_mask));
        
        assert(!PACSIGNED(mc->__ss.__fp, va_mask));
        assert(!PACSIGNED(mc->__ss.__lr, va_mask));
        assert(!PACSIGNED(mc->__ss.__sp, va_mask));
        assert(!PACSIGNED(mc->__ss.__pc, va_mask));
        
        /**
         * Re-sign link register if it contained a valid PAC
         * that was stripped before checkpointing
         */
        reg_ctx_t *c = &hdrs[nr_hdrs - 1].ctx;
        if (c->pacmap & (UINT64_C(1) << 30))
                PACIB(mc->__ss.__lr, mc->__ss.__sp);
        
        if (setcontext(&c->uc) < 0) {
                perror("setcontext");
                return -1;
        }

        return 0;
}

int grow_stack(char *ckptfile, int levels)
{
        if (levels > 0)
                return grow_stack(ckptfile, levels - 1);
        else {
                return restart(ckptfile);
        }
}

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./restart <ckpt-file>");
                exit(1);
        }

        if (grow_stack(argv[1], 1000) < 0) {
                fprintf(stderr, "Restart failed. Exiting...\n");
                exit(1);
        }

        exit(0);
}
