/* vm_restore.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/readckpt.h"
#include "xnd/util/io.h"
#include <dlfcn.h>
#include <errno.h>

extern mach_port_t      mach_task_self_;
extern mach_port_t      task_self_trap(void);
extern uintptr_t        __stack_chk_guard;

int ckpt_vm_mark_regions(void)
{
        kern_return_t                   ret;
        mach_vm_address_t               addr = 0;
        mach_vm_size_t                  size = 0;
        natural_t                       depth = 0;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t          count;
        Dl_info                         dl_info;

        for (;;) {
                count = VM_REGION_SUBMAP_INFO_COUNT_64;
                ret = mach_vm_region_recurse(
                        mach_task_self(), &addr, &size, &depth,
                        (vm_region_recurse_info_t)&info, &count
                );

                if (ret != KERN_SUCCESS) {
                        break;
                } else if (info.is_submap) {
                        depth++;
                        continue;
                }

                if (PAGEZERO(addr, size) ||
                    DYLD_SHARED_CACHE_REGION(addr, size)) {
                        addr += size;
                        continue;
                } else if (dladdr((const void *)addr, &dl_info) == 0 ||
                           !strstr(dl_info.dli_fname, "xnd_restart")) {
                        addr += size;
                        continue;
                }
                
                ret = mach_vm_inherit(mach_task_self(), addr, size,
                                      RESTART_REGION_INHERIT_FLAG);
                if (ret != KERN_SUCCESS) {
                        xnd_warn("mach_vm_inherit: %s\n",
                                 mach_error_string(ret));
                        return -1;
                }

                ret = mach_vm_behavior_set(mach_task_self(), addr, size,
                                           RESTART_REGION_BEHAVIOR_FLAG);
                if (ret != KERN_SUCCESS) {
                        xnd_warn("mach_vm_behavior_set: %s\n",
                                 mach_error_string(ret));
                        return -1;
                }

                addr += size;
        }

        return 0;
}

static __no_stack_protector __always_inline
int ckpt_vm_restore_pages(int fd, struct xnd_vm_region *region)
{
        void                    *addr;
        ssize_t                 bytes;
        struct xnd_vm_page      pages[region->pages_dirtied];

        for (uint idx = 0; idx < region->pages_dirtied; idx++) {
                bytes = readall(fd, pages + idx, sizeof(pages[idx]));
                if (bytes != sizeof(pages[idx])) {
                        return -1;
                }
                addr = region->start + pages[idx].offset;
                bytes = readall(fd, addr, VM_PAGE_SIZE);
                if (bytes != VM_PAGE_SIZE) {
                        return -1;
                }
        }

        return 0;
}

static int ckpt_vm_restore_region_pages(int fd, struct xnd_vm_region *region)
{
        void            *end;
        vm_prot_t       prot;
        bool            writable;
        uintptr_t       saved_stack_chk_guard;

        saved_stack_chk_guard = __stack_chk_guard;

        /**
         * If this region is not current writable, get a private, writable
         * copy via mach_vm_protect with VM_PROT_READ | VM_PROT_WRITE |
         * VM_PROT_COPY.
         */
        writable = (region->prot & VM_PROT_WRITE);
        if (!writable) {
                prot = VM_PROT_DEFAULT | VM_PROT_COPY;
                if (ckpt_vm_protect(region, false, prot) != 0) {
                        return -1;
                }
        }

        if (ckpt_vm_restore_pages(fd, region) != 0) {
                return -1;
        }
        
        end = region->start + region->size;
        /**
         * If cached mach task port (mach_task_self_) was overwritten by
         * restoring this region, restore the correct task port using
         * task_self_trap() to get the true task port value.
         */
        if (IN_VM_RANGE(&mach_task_self_, region->start, end)) {
                mach_task_self_ = task_self_trap();
                xnd_assert(mach_task_self() == task_self_trap());
        }
        
        /**
         * If __stack_chk_guard was overwritten by a restored page,
         * refresh its value to prevent __stack_chk_fail.
         */
        if (IN_VM_RANGE(&__stack_chk_guard, region->start, end)) {
                __stack_chk_guard = saved_stack_chk_guard;
        }

        if (!writable) {
                if (ckpt_vm_protect(region, false, region->prot) != 0) {
                        return -1;
                }
        }

        return 0;
}

int ckpt_vm_restore_region(int fd, struct xnd_vm_region *region)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        ssize_t                 bytes;
        void                    *end;

        if (ONLY_RESTORE_DIRTY_PAGES(region)) {
                return ckpt_vm_restore_region_pages(fd, region);
        }

        xnd_assert(region->start != NULL);
        addr = (mach_vm_address_t)region->start;
        size = (mach_vm_size_t)region->size;
        
        /* Allocate checkpointed memory region */
        kr = mach_vm_map(mach_task_self(), &addr, size, 0,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE |
                         VM_MAKE_TAG(region->tag),
                         MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_DEFAULT,
                         VM_PROT_ALL, region->inherit);
        
        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_map: %s\n"
                          "(%p-%p %zu %s/%s %s)\n",
                          mach_error_string(kr), region->start,
                          region->start + region->size, region->size,
                          VM_PROT_STRING(VM_PROT_DEFAULT),
                          VM_PROT_STRING(VM_PROT_ALL),
                          VM_INHERIT_STRING(region->inherit));
                return -1;
        }
        
        xnd_assert((void *)addr == region->start);
        bytes = readall(fd, (void *)addr, region->size);
        if (bytes != region->size) {
                return -1;
        }

        /**
         * If restored region contains the cached mach port, restore the
         * correct mach port for this process via task_self_trap().
         */
        end = region->start + region->size;
        if (IN_VM_RANGE(&mach_task_self_, region->start, end)) {
                mach_task_self_ = task_self_trap();
                xnd_assert(mach_task_self() == task_self_trap());
        }

        if (region->prot != VM_PROT_DEFAULT) {
                if (ckpt_vm_protect(region, false, region->prot) != 0) {
                        return -1;
                }
        }
        
        if (region->max_prot != VM_PROT_ALL) {
                if (ckpt_vm_protect(region, true, region->max_prot) != 0) {
                        return -1;
                }
        }
        
        return 0;
}
