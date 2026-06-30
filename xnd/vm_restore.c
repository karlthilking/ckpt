/* vm_restore.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/readckpt.h"
#include "xnd/util/io.h"
#include <dlfcn.h>
#include <errno.h>

extern mach_port_t task_self_trap(void);
extern mach_port_t thread_self_trap(void);

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

int ckpt_vm_restore_region(int fd, const struct xnd_vm_region *region)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        ssize_t                 bytes;

        xnd_assert(region->start != NULL && region->end != NULL);
        xnd_assert(mach_task_self() == task_self_trap());
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
                          region->end, region->size,
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

        if (region->prot != VM_PROT_DEFAULT) {
                kr = mach_vm_protect(mach_task_self(), addr, size,
                                     FALSE, region->prot);
                if (kr != KERN_SUCCESS) {
                        xnd_error("mach_vm_protect: %s\n",
                                mach_error_string(kr));
                        return -1;
                }
        }

        if (region->max_prot != VM_PROT_ALL) {
                kr = mach_vm_protect(mach_task_self(), addr, size,
                                     TRUE, region->max_prot);
                if (kr != KERN_SUCCESS) {
                        xnd_error("mach_vm_protect: %s\n",
                                  mach_error_string(kr));
                        return -1;
                }
        }
        
        return 0;
}
