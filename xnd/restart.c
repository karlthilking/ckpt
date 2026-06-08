/* restart.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include <ucontext.h>
#include <sys/mman.h>
#include "readckpt.h"
#include "ckpt.h"
#include "xnd.h"
#include "pac.h"
#include "vm_region.h"
#include "shared_cache.h"

__noreturn noinline void restart(int fd)
{
        int                     retval;
        u64                     fp;
        ckpt_metadata_t         meta;

        retval = ckpt_vm_mark_regions();
        if (retval < 0 || readall(fd, &meta, sizeof(meta)) < 0 ||
            shared_cache_check(&meta.shared_cache_info) < 0) {
                exit(EXIT_FAILURE);
        }

        ckpt_header_t           headers[meta.nr_headers];
        ckpt_vm_region_t        regions[meta.nr_regions];
        ckpt_context_t          ctx;

        retval = read_ckpt(fd, &meta, headers, regions, &ctx);
        if (retval < 0) {
                fprintf(stderr, 
                        "Failed to read checkpoint file, "
                        "aborting restart...\n");
                exit(EXIT_FAILURE);
        }
        
        fp = get_ucontext_fp(&ctx);
        if (PTRAUTH_SIGNED(fp)) {
                XPACD(fp);
        }

        pac_resign_frames((u64 *)fp);
        pac_patch_context(&ctx);

        if (setcontext(&ctx) < 0) {
                err(EXIT_FAILURE, "setcontext");
        }
        
        abort();
}

/**
 * jump:
 * Allocate
 *  Jump to a temporary stack and initiate the restart.
 */
__noreturn void jump(int fd)
{
        kern_return_t           ret;
        void                    *sp;
        mach_vm_address_t       addr = 0x320000000;
        const mach_vm_size_t    size = 1024 * 1024;
        
        /**
         * Make VM object purgable and associate VM_REGION_RESTART_STACK
         * user_tag with mapping s.t. memory region checkpoint path
         * will know to discard this region.
         */
        ret = mach_vm_map(mach_task_self(), &addr, size, 0,
                          VM_FLAGS_FIXED | VM_FLAGS_PURGABLE |
                          VM_MAKE_TAG(VM_MEMORY_RESTART_STACK),
                          MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_DEFAULT,
                          VM_PROT_ALL, VM_INHERIT_NONE);

        if (ret != KERN_SUCCESS) {
                errx(EXIT_FAILURE, "mach_vm_map: %s\n",
                     mach_error_string(ret));
        }
        
        sp = (void *)(addr + size);
        
        /* Switch to temporary stack and call restart function */
        asm volatile(
                "mov    sp, %[sp]       \n"
                "mov    x0, %[fildes]   \n"
                "blraaz %[restart]      \n"
                :
                : [sp] "r" (sp), [fildes] "r" ((long)fd),
                  [restart] "r" (restart)
        );

        unreachable();
}

__noreturn int main(int argc, char **argv)
{
        int fd;

        if (argc != 2) {
                fprintf(stderr, "Usage: ./ckpt -r [ckpt-file]\n");
                exit(EXIT_FAILURE);
        } else if ((fd = open(argv[1], O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open (%s)", argv[1]);

        printf("Restarting from %s (pid=%d)\n", argv[1], getpid());
        jump(fd);

        unreachable();
}
