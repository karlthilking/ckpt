/* vm_restore.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include "xnd/readckpt.h"
#include "xnd/syscall.h"
#include <dlfcn.h>
#include <errno.h>

extern long __stack_chk_guard;
extern mach_port_t mach_task_self_;
extern mach_port_t task_self_trap(void);

static inline int ckpt_vm_map_region(struct xnd_vm_region *);
static inline int ckpt_vm_refresh(struct xnd_vm_region *);
static inline int ckpt_vm_restore_pages(int, struct xnd_vm_region *);
static int ckpt_vm_restore_region_pages(int, struct xnd_vm_region *);

static inline int ckpt_vm_mark(mach_vm_address_t addr, mach_vm_size_t size)
{
        kern_return_t kr;

        kr = mach_vm_inherit(mach_task_self(), addr, size,
                             RESTART_REGION_INHERIT_FLAG);
        if (kr != KERN_SUCCESS) {
                xnd_warn("mach_vm_inherit: %s\n", mach_error_string(kr));
                return -1;
        }

        kr = mach_vm_behavior_set(mach_task_self(), addr, size,
                                  RESTART_REGION_BEHAVIOR_FLAG);
        if (kr != KERN_SUCCESS) {
                xnd_warn("mach_vm_behavior_set: %s\n",
                         mach_error_string(kr));
                return -1;
        }

        return 0;
}

int ckpt_vm_mark_regions(void)
{
        kern_return_t kr;
        mach_vm_address_t addr = 0;
        mach_vm_size_t size = 0;
        natural_t depth = 0;
        mach_msg_type_number_t count;
        vm_region_submap_info_data_64_t info;
	uintptr_t dyld_start, dyld_end;

	/*
	 * dyld will map itself above xnd_restart_internal, placing its
	 * memory segments in a hole between the restart binary's image
	 * and xnd_restart_internal's temporary stack. dyld's memory
	 * segments should be skipped when marking restart regions to
	 * prevent a segfault, for example, if a symbol is bound to
	 * the copy dyld in xnd_restart_internal.
	 */
	dyld_start = XND_RESTART_LINKEDIT + XND_RESTART_LINKEDIT_SIZE;
	dyld_end = dyld_start + DYLD_RESERVE_SIZE;

        for (;;) {
                count = VM_REGION_SUBMAP_INFO_COUNT_64;
                kr = mach_vm_region_recurse(mach_task_self(), &addr,
                                            &size, &depth,
                                            (vm_region_recurse_info_t)&info,
                                            &count);
                if (kr != KERN_SUCCESS)
                        break;
                else if (info.is_submap)
                        continue;

		if (PAGEZERO(addr, size) ||
		    DYLD_SHARED_CACHE_REGION(addr, size) ||
		    (addr >= dyld_start && addr + size < dyld_end)) {
			addr += size;
			continue;
		}

		if (ckpt_vm_mark(addr, size) < 0)
			return -1;

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
                bytes = sys_readall(fd, pages + idx, sizeof(pages[idx]));
                if (bytes != sizeof(pages[idx])) {
                        return -1;
                }
                addr = region->start + pages[idx].offset;
                bytes = sys_readall(fd, addr, VM_PAGE_SIZE);
                if (bytes != VM_PAGE_SIZE) {
                        return -1;
                }
        }

        return 0;
}

static int ckpt_vm_restore_region_pages(int fd, struct xnd_vm_region *region)
{
        vm_prot_t prot;
        bool writable = (region->prot & VM_PROT_WRITE) != 0;
        uintptr_t end = (uintptr_t)region->start + region->size;

        writable = (region->prot & VM_PROT_WRITE) != 0;
        if (NEEDS_REMAP_BEFORE_RESTORE(region)) {
                if (ckpt_vm_map_region(region) != 0)
                        return -1;
        } else if (!writable) {
                /**
                 * If this region is not current writable, get a private,
                 * writable copy via mach_vm_protect with
                 * VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY.
                 */
                prot = VM_PROT_DEFAULT | VM_PROT_COPY;
                if (ckpt_vm_protect(region, false, prot) != 0)
                        return -1;
        }

        if (ckpt_vm_restore_pages(fd, region) != 0) {
                return -1;
        }

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
                 * If region had to be mapped in (not already in address
                 * space by default), reset the region's protections to
                 * reflect its original protections before checkpoint.
                 * (by default, it would have been mapped in with rw-/rwx)
                 */
                if (ckpt_vm_refresh(region) != 0)
                        return -1;
        } else if (!writable) {
                /**
                 * Otherwise, if the region was freshly mapped in, but
                 * had to be made writable (to restore checkpoint data),
                 * reset its protections so that the region will no longer
                 * be writable.
                 */
                if (ckpt_vm_protect(region, false, region->prot) != 0)
                        return -1;
        }

        return 0;
}

static inline int ckpt_vm_map_region(struct xnd_vm_region *region)
{
        kern_return_t kr;
        mach_vm_address_t addr;
        mach_vm_size_t size;

        addr = (mach_vm_address_t)region->start;
        size = (mach_vm_size_t)region->size;
        kr = mach_vm_map(mach_task_self(), &addr, size, 0,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE |
                         VM_MAKE_TAG(region->tag), MEMORY_OBJECT_NULL,
                         0, FALSE, VM_PROT_DEFAULT, VM_PROT_ALL,
                         VM_INHERIT_DEFAULT);

        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_map: %s\n"
                          "(%p-%p %zu %s/%s %s)\n",
                          mach_error_string(kr), region->start,
                          region->start + region->size, region->size,
                          VM_PROT_STRING(VM_PROT_DEFAULT),
                          VM_PROT_STRING(VM_PROT_ALL),
                          VM_INHERIT_STRING(VM_INHERIT_DEFAULT));
                return -1;
        }

        xnd_assert((mach_vm_address_t)region->start == addr);
        return 0;
}

/**
 * ckpt_vm_refresh:
 *  Refresh region protections and inheritance to reflect their
 *  original attributes (i.e. before checkpoint)
 */
static inline int ckpt_vm_refresh(struct xnd_vm_region *region)
{
        if (region->prot != VM_PROT_DEFAULT) {
                if (ckpt_vm_protect(region, false, region->prot) != 0)
                        return -1;
        }

        if (region->max_prot != VM_PROT_ALL) {
                if (ckpt_vm_protect(region, true, region->max_prot) != 0)
                        return -1;
        }

        if (region->inherit != VM_INHERIT_DEFAULT) {
                kern_return_t kr;
                mach_vm_address_t addr = (mach_vm_address_t)region->start;
                mach_vm_size_t size = (mach_vm_size_t)region->size;
                kr = mach_vm_inherit(mach_task_self(), addr, size,
                                     region->inherit);

                if (kr != KERN_SUCCESS) {
                        xnd_warn("mach_vm_inherit: %s (%s -> %s)\n",
                                 mach_error_string(kr),
                                 VM_INHERIT_STRING(VM_INHERIT_NONE),
                                 VM_INHERIT_STRING(region->inherit));
                        return -1;
                }
        }

        return 0;
}

int ckpt_vm_restore_region(int fd, struct xnd_vm_region *region)
{
        ssize_t bread;
        uintptr_t end;

        if (ONLY_RESTORE_DIRTY_PAGES(region))
                return ckpt_vm_restore_region_pages(fd, region);

        if (ckpt_vm_map_region(region) != 0)
                return -1;

        bread = sys_readall(fd, region->start, region->size);
        if (bread != region->size)
                return -1;

        /* Restore cached mach port if overwritten */
        end = (uintptr_t)region->start + region->size;
        if (IN_VM_RANGE(&mach_task_self_, region->start, end))
                mach_task_self_ = task_self_trap();

        if (ckpt_vm_refresh(region) != 0)
                return -1;

        return 0;
}

mach_vm_address_t ckpt_vm_find_ubc_region(mach_vm_size_t *out)
{
        kern_return_t kr;
        mach_msg_type_number_t count;
        vm_region_submap_info_data_64_t info;
        mach_vm_address_t addr = 0;
        mach_vm_size_t size = 0;
        natural_t depth = 0;

        for (;;) {
                count = VM_REGION_SUBMAP_INFO_COUNT_64;
                kr = mach_vm_region_recurse(mach_task_self(), &addr,
                        &size, &depth, (vm_region_recurse_info_t)&info,
                        &count);
                if (kr != KERN_SUCCESS)
                        break;
                else if (info.is_submap)
                        continue;

                if (info.user_tag == VM_KERN_MEMORY_UBC) {
                        if (out)
                                *out = size;
                        return addr;
                }

                addr += size;
        }

        return (mach_vm_address_t)0;
}

int ckpt_vm_remove_xnd_guard(void)
{
        kern_return_t kr;
        mach_vm_address_t addr = XND_GUARD_ADDR;
        mach_vm_size_t size = XND_GUARD_SIZE;

        kr = mach_vm_deallocate(mach_task_self(), addr, size);
        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_deallocate: %s\n",
                          mach_error_string(kr));
                return -1;
        }

        return 0;
}
