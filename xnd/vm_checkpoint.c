/* vm_checkpoint.c */
#include <stdio.h>
#include "vm_region.h"

int ckpt_vm_valid_region(vm_region_submap_info_data_64_t *info,
                         mach_vm_address_t addr, mach_vm_size_t size)
{
        if (PAGEZERO(addr, size) || info->max_protection == VM_PROT_NONE) {
                return 0;
        } else if (RESTART_REGION(info)) {
                /* Restart region, can be discared */
                return 0;
        } else if (DYLD_SHARED_CACHE_REGION(addr, size)) {
                if (info->pages_dirtied) {
                        return 1;
                }
                return 0;
        }

        switch (info->user_tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_NANO:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_MEDIUM:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_HUGE:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
        case VM_MEMORY_MALLOC_PROB_GUARD:
        case VM_MEMORY_STACK:
                return 1;
        case VM_MEMORY_DYLD:
        case VM_MEMORY_DYLD_MALLOC:
                if (info->pages_resident && info->pages_dirtied) {
                        xnd_assert(info->max_protection & VM_PROT_WRITE);
                        return 1;
                }
                return 0;
        case VM_MEMORY_REALLOC:
        case VM_MEMORY_GUARD:
        case VM_MEMORY_SHARED_PMAP:
        case VM_MEMORY_UNSHARED_PMAP:
                return 0;
        case VM_MEMORY_DYLIB:
        case VM_MEMORY_OS_ALLOC_ONCE:
                return 1;
        case VM_MEMORY_FOUNDATION:
        case VM_MEMORY_JAVA:
        case VM_MEMORY_GLSL:
        case VM_MEMORY_OPENCL:
        case VM_MEMORY_LIBDISPATCH:
        case VM_MEMORY_ACCELERATE:
        case VM_MEMORY_SWIFT_RUNTIME:
                if (info->protection & VM_PROT_WRITE) {
                        return info->pages_resident && info->pages_dirtied;
                }
                return 1;
        default:
                break;
        }
        
        return 1;
}

u32 ckpt_vm_save_regions(struct xnd_vm_region *regions)
{
        kern_return_t                   ret;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t          count;
        struct xnd_vm_region            *rgn;
        u32                             region_count    = 0;
        mach_vm_address_t               addr            = 0;
        mach_vm_size_t                  size            = 0;
        natural_t                       depth           = 0;

        for (;;) {
                count = VM_REGION_SUBMAP_INFO_COUNT_64;
                ret = mach_vm_region_recurse(
                        mach_task_self(), &addr, &size, &depth,
                        (vm_region_recurse_info_t)&info, &count
                );

                if (ret != KERN_SUCCESS)
                        break;
                else if (info.is_submap) {
                        depth++;
                        continue;
                } else if (!ckpt_vm_valid_region(&info, addr, size)) {
                        addr += size;
                        continue;
                }

                rgn = regions + region_count;
                rgn->start = (void *)addr;
                rgn->size = (size_t)size;
                rgn->inherit = info.inheritance;
                rgn->prot = info.protection;
                rgn->max_prot = info.max_protection;
                rgn->mode = info.share_mode;
                rgn->tag = info.user_tag;
                rgn->pages_dirtied = info.pages_dirtied;
                
                region_count++;
                addr += size;
        }
        
        return region_count;
}

void ckpt_vm_deallocate_regions(void)
{
        kern_return_t                   ret;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t          count;
        mach_vm_address_t               addr    = 0;
        mach_vm_size_t                  size    = 0;
        natural_t                       depth   = 0;
        
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
                
                if (!RESTART_REGION(&info)) {
                        addr += size;
                        continue;
                }
                
                /* Only deallocate restart stack and executable segments */
                if (info.user_tag != VM_MEMORY_RESTART_STACK &&
                    info.protection != (VM_PROT_READ | VM_PROT_EXECUTE)) {
                        addr += size;
                        continue;
                }
                        
                ret = mach_vm_deallocate(mach_task_self(), addr, size);
                if (ret != KERN_SUCCESS) {
                        xnd_warn("mach_vm_deallocate: %s\n",
                                 mach_error_string(ret));
                } else {
                        xnd_trace("Deallocated restart region 0x%llx-0x%llx\n",
                                  addr, addr + size);
                }

                addr += size;
        }
}
