/* xnd_restart_internal.c */
#include "xnd/xnd.h"
#include "xnd/readckpt.h"
#include "xnd/pac.h"
#include "xnd/vm_region.h"
#include "xnd/shared_cache.h"
#include "xnd/util/io.h"
#include "xnd/util/log.h"
#include "xnd/platform/ucontext/ucontext.h"

#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ucontext.h>
#include <sys/mman.h>

__noreturn noinline void restart(int fd)
{
        int                     retval;
        struct xnd_ckpt_header  header;

        retval = ckpt_vm_mark_regions();
        if (retval < 0) {
                xnd_warn("Failed to mark restart regions\n");
        }

        retval = readall(fd, &header, sizeof(header));
        if (retval < 0) {
                xnd_error("Failed to read checkpoint header\n");
                exit(XND_EXIT_FAILURE);
        }

        if (!xnd_ckptfile_valid(&header)) {
                xnd_error("Checkpoint file is invalid\n");
                exit(XND_EXIT_FAILURE);
        }

        enum xnd_ckpt_entry     entries[header.entry_count];
        struct xnd_vm_region    regions[header.region_count];
        ucontext_t              uctx;

        retval = read_ckpt(fd, &header, entries, regions, &uctx);
        if (retval < 0) {
                xnd_error("Failed to read checkpoint file, aborting...\n");
                exit(XND_EXIT_FAILURE);
        }

        pac_resign_frames((u64 *)get_ucontext_fp(&uctx));
        xnd_setcontext(&uctx);

        unreachable();
}

/**
 * jump:
 *  Jump to a temporary stack and initiate the restart.
 */
__noreturn void jump(int fd)
{
        kern_return_t           ret;
        void                    *sp;
        const mach_vm_size_t    size = 1024 * 1024;
        mach_vm_address_t       addr = XND_RESTART_STACK;

        xnd_trace("Allocating temporary stack 0x%llx-0x%llx\n",
                  addr, addr + size);
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
                xnd_error("mach_vm_map: %s\n", mach_error_string(ret));
                exit(XND_EXIT_FAILURE);
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
                xnd_error("restart should not be invoked directly!\n"
                          "Usage: ./xnd_run -r <ckpt-file>\n");
                exit(XND_EXIT_FAILURE);
        }

#if DEVELOPMENT || DEBUG
        xnd_log_mach_port_info();
        xnd_log_shared_cache_info();
#endif

        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
                xnd_error("open(%s, ...): %s\n", argv[1], strerror(errno));
                exit(XND_EXIT_FAILURE);
        }

        xnd_printf("Restarting from %s (pid=%d)\n", argv[1], getpid());
        jump(fd);

        unreachable();
}
