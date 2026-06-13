/* vm_restore.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/readckpt.h"
#include <dlfcn.h>

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

int ckpt_vm_restore_region(int fd, const ckpt_vm_region_t *region)
{
        kern_return_t           ret;
        mach_vm_address_t       addr;
        
        xnd_assert(region->start != NULL && region->end != NULL);
        addr = (mach_vm_address_t)region->start;
        
        /* Allocate checkpointed memory region */
        ret = mach_vm_map(mach_task_self(), &addr, region->size, 0,
                          VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE |
                          VM_MAKE_TAG(region->tag),
                          MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_DEFAULT,
                          VM_PROT_ALL, region->inherit);
        
        if (ret != KERN_SUCCESS) {
                xnd_error("mach_vm_map: %s\n", mach_error_string(ret));
                return -1;
        }
        
        xnd_assert((void *)addr == region->start);
        if (readall(fd, (void *)addr, region->size) < 0)
                return -1;

        if (region->prot != VM_PROT_DEFAULT &&
            ckpt_vm_protect(region, 0, region->prot) < 0)
                return -1;
        else if (region->max_prot != VM_PROT_ALL &&
                 ckpt_vm_protect(region, 1, region->max_prot) < 0)
                return -1;

        return 0;
}
