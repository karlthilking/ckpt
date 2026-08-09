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
static inline int ckpt_vm_refresh_region(struct xnd_vm_region *);
static int ckpt_vm_restore_pages(int, struct xnd_vm_region *);
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
		    DYLD_SHARED_CACHE_REGION(addr, size)) {
			addr += size;
			continue;
		}

		if (ckpt_vm_mark(addr, size) < 0)
			return -1;

                addr += size;
        }

        return 0;
}

static inline int ckpt_vm_refresh_page(struct xnd_vm_region *rgn,
				       struct xnd_vm_page *page)
{
	kern_return_t kr;
	mach_vm_address_t addr;

	addr = (mach_vm_address_t)((uintptr_t)rgn->start + page->offset);
	if (rgn->prot != VM_PROT_DEFAULT) {
		kr = mach_vm_protect(mach_task_self(), addr, VM_PAGE_SIZE,
				     FALSE, rgn->prot);
		if (kr != KERN_SUCCESS) {
			xnd_error("mach_vm_protect: %s (%s)\n",
				  mach_error_string(kr),
				  vm_page_string(rgn, page));
			return -1;
		}
	}

	if (rgn->max_prot != VM_PROT_ALL) {
		kr = mach_vm_protect(mach_task_self(), addr, VM_PAGE_SIZE,
				     TRUE, rgn->max_prot);
		if (kr != KERN_SUCCESS) {
			xnd_error("mach_vm_protect: %s (%s)\n",
				  mach_error_string(kr),
				  vm_page_string(rgn, page));
			return -1;
		}
	}

	if (rgn->inherit != VM_INHERIT_DEFAULT) {
		kr = mach_vm_inherit(mach_task_self(), addr, VM_PAGE_SIZE,
				     rgn->inherit);
		if (kr != KERN_SUCCESS) {
			xnd_error("mach_vm_inherit: %s (%s)\n",
				  mach_error_string(kr),
				  vm_page_string(rgn, page));
			return -1;
		}
	}

	return 0;
}

static inline int ckpt_vm_map_page(struct xnd_vm_region *rgn,
				   struct xnd_vm_page *page)
{
	kern_return_t kr;
	mach_vm_address_t addr;

	addr = (mach_vm_address_t)((uintptr_t)rgn->start + page->offset);
	kr = mach_vm_map(mach_task_self(), &addr, VM_PAGE_SIZE, 0,
			 VM_FLAGS_FIXED | VM_MAKE_TAG(rgn->tag),
			 MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_DEFAULT,
			 VM_PROT_ALL, VM_INHERIT_DEFAULT);

	if (kr != KERN_SUCCESS) {
		xnd_error("mach_vm_map: %s (%s)\n", mach_error_string(kr),
			  vm_page_string(rgn, page));
		return -1;
	}

	xnd_assert((mach_vm_address_t)rgn->start == addr);
	return 0;
}

static int ckpt_vm_page_restore(int fd, struct xnd_vm_region *rgn,
			        struct xnd_vm_page *page)
{
	void *addr;
	ssize_t bytes;

	addr = (void *)((uintptr_t)rgn->start + page->offset);
	bytes = sys_readall(fd, addr, VM_PAGE_SIZE);
	if (bytes == VM_PAGE_SIZE)
		return 0;

	/*
	 * If read failed with EFAULT, we'll try to recover by mapping
	 * in the page ourselves. Otherwise, the error is not handled.
	 */
	if (errno != EFAULT) {
		xnd_perror("read");
		return -1;
	}

	/*
	 * If the error was EFAULT, we shouldn't witness a partial
	 * read. The page should either be present or not accessible
	 * at all. We will try to map the page in now and see if the
	 * read succeeds.
	 */
	xnd_assert(bytes == 0);
	if (ckpt_vm_map_page(rgn, page) != 0)
		return -1;

	bytes = sys_readall(fd, addr, VM_PAGE_SIZE);
	if (bytes != VM_PAGE_SIZE) {
		xnd_error("read: %s\n", strerror(errno));
		xnd_error("Failed to restore page: %s\n",
			  vm_page_string(rgn, page));
		return -1;
	}

	if (ckpt_vm_refresh_page(rgn, page) != 0)
		xnd_warn("Fail to restore page attributes\n");

	return 0;
}

static int ckpt_vm_restore_pages(int fd, struct xnd_vm_region *rgn)
{
	ssize_t bytes;
	struct xnd_vm_page pages[rgn->pages_dirtied];

	for (uint idx = 0; idx < rgn->pages_dirtied; idx++) {
		bytes = sys_readall(fd, &pages[idx], sizeof(pages[idx]));
		if (bytes != sizeof(pages[idx])) {
			xnd_perror("read");
			return -1;
		}
		if (ckpt_vm_page_restore(fd, rgn, &pages[idx]) != 0)
			return -1;
	}

	return 0;
}

static int ckpt_vm_restore_region_pages(int fd, struct xnd_vm_region *region)
{
	int ret;
        vm_prot_t prot;
        uintptr_t end = (uintptr_t)region->start + region->size;

	/*
	 * Memory regions in the dyld shared cache should not need
	 * to be mapped in. However, we must ensure that the region
	 * is writable before we try to restore memory. VM_PROT_COPY
	 * handles the case where we must get a private copy in order
	 * to make the region writable.
	 *
	 * For regions that are not in the shared cache, we must
	 * obtain a fresh region before we restore memory.
	 */
	if (DYLD_SHARED_CACHE_REGION(region->start, region->size)) {
		prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY;
		ret = ckpt_vm_protect(region, false, prot);
	} else {
		ret = ckpt_vm_map_region(region);
	}

	if (ret != 0)
		return -1;
	if (ckpt_vm_restore_pages(fd, region) != 0)
		return -1;

        /*
         * If cached mach task port (mach_task_self_) was overwritten by
         * restoring this region, restore the correct task port using
         * task_self_trap() to get the true task port value.
         */
        if (IN_VM_RANGE(&mach_task_self_, region->start, end)) {
                mach_task_self_ = task_self_trap();
                xnd_assert(mach_task_self() == task_self_trap());
        }

	/*
	 * For regions in the dyld shared cache that were not
	 * originally writable, restore the region's orignal
	 * protection attribute.
	 *
	 * For regions that were mapped in, protection, max protection,
	 * and inheritance attributes must be refreshed to restore
	 * the original attributes when we took a checkpoint.
	 */
	if (DYLD_SHARED_CACHE_REGION(region->start, region->size)) {
		prot = region->prot;
		ret = ckpt_vm_protect(region, false, prot);
	} else {
		ret = ckpt_vm_refresh_region(region);
	}

	return ret;
}

static inline int ckpt_vm_map_region(struct xnd_vm_region *region)
{
        kern_return_t kr;
        mach_vm_address_t addr = (mach_vm_address_t)region->start;
        mach_vm_size_t size = (mach_vm_size_t)region->size;

        kr = mach_vm_map(mach_task_self(), &addr, size, 0,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE |
                         VM_MAKE_TAG(region->tag), MEMORY_OBJECT_NULL,
                         0, FALSE, VM_PROT_DEFAULT, VM_PROT_ALL,
                         VM_INHERIT_DEFAULT);
        if (kr != KERN_SUCCESS) {
		xnd_error("mach_vm_map: %s (%s)\n", mach_error_string(kr),
			  vm_region_string(region));
                return -1;
        }

        xnd_assert((mach_vm_address_t)region->start == addr);
        return 0;
}

/*
 * ckpt_vm_refresh_region:
 *  Refresh region protections and inheritance to reflect their
 *  original attributes (i.e. before checkpoint)
 */
static inline int ckpt_vm_refresh_region(struct xnd_vm_region *region)
{
	kern_return_t kr;
	mach_vm_address_t addr;
	mach_vm_size_t size;

        if (region->prot != VM_PROT_DEFAULT) {
                if (ckpt_vm_protect(region, false, region->prot) != 0)
                        return -1;
        }

        if (region->max_prot != VM_PROT_ALL) {
                if (ckpt_vm_protect(region, true, region->max_prot) != 0)
                        return -1;
        }

        if (region->inherit != VM_INHERIT_DEFAULT) {
		addr = (mach_vm_address_t)region->start;
		size = (mach_vm_size_t)region->size;
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

        if (ckpt_vm_refresh_region(region) != 0)
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
