/* vm_restore.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/readckpt.h"
#include "xnd/util/io.h"
#include <dlfcn.h>
#include <errno.h>

static inline int ckpt_vm_map_region(struct xnd_vm_region *);
static inline int ckpt_vm_refresh_protections(struct xnd_vm_region *);
static inline int ckpt_vm_restore_pages(int, struct xnd_vm_region *);
static int ckpt_vm_restore_region_pages(int, struct xnd_vm_region *);

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

static inline int ckpt_vm_restore_pages(int fd, struct xnd_vm_region *region)
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

        if (NEEDS_REMAP_BEFORE_RESTORE(region)) {
                if (ckpt_vm_map_region(region) != 0) {
                        return -1;
                }
        } else if ((writable = (region->prot & VM_PROT_WRITE)) == false) {
                /**
                 * If this region is not current writable, get a private, 
                 * writable copy via mach_vm_protect with 
                 * VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY.
                 */
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
        
        if (NEEDS_REMAP_BEFORE_RESTORE(region)) {
                /**
                 * If region had to be mapped in (not already in address space
                 * by default), reset the region's protections to reflect
                 * its original protections before checkpoint.
                 * (by default, it would have been mapped in with rw-/rwx)
                 */
                if (ckpt_vm_refresh_protections(region) != 0) {
                        return -1;
                }
        } else if (writable == false) {
                /**
                 * Otherwise, if the region was freshly mapped in, but
                 * had to be made writable (to restore checkpoint data), 
                 * reset its protections so that the region will no longer
                 * be writable.
                 */
                if (ckpt_vm_protect(region, false, region->prot) != 0) {
                        return -1;
                }
        }

        return 0;
}

static inline int ckpt_vm_map_region(struct xnd_vm_region *region)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        
        addr = (mach_vm_address_t)region->start;
        size = (mach_vm_size_t)region->size;
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
        
        return ((uintptr_t)addr == (uintptr_t)region->start ? 0 : -1);
}

static inline int ckpt_vm_refresh_protections(struct xnd_vm_region *region)
{
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

int ckpt_vm_restore_region(int fd, struct xnd_vm_region *region)
{
        ssize_t bytes;
        void    *end;

        if (ONLY_RESTORE_DIRTY_PAGES(region)) {
                return ckpt_vm_restore_region_pages(fd, region);
        }

        if (ckpt_vm_map_region(region) != 0) {
                return -1;
        }
        
        bytes = readall(fd, region->start, region->size);
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

        if (ckpt_vm_refresh_protections(region) != 0) {
                return -1;
        }
        
        return 0;
}
