/* vm_common.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include <mach/mach_init.h>
#include <stdio.h>

static char vm_error_buf[512];

int ckpt_vm_protect(struct xnd_vm_region *region, bool max, vm_prot_t prot)
{
	kern_return_t kr;
	mach_vm_size_t size;
	mach_vm_address_t addr;

	addr = (mach_vm_address_t)region->start;
	size = (mach_vm_size_t)region->size;

	kr = mach_vm_protect(mach_task_self(), addr, size, max, prot);
	if (kr != KERN_SUCCESS) {
		xnd_error("mach_vm_protect: %s (%s)\n",
			  mach_error_string(kr),
			  vm_region_string(region));
		return -1;
	}

        return 0;
}

const char *vm_page_string(struct xnd_vm_region *region,
			   struct xnd_vm_page *page)
{
	void *start, *end;

	start = (void *)((char *)region->start + page->offset);
	end = (void *)((char *)start + VM_PAGE_SIZE);

	snprintf(vm_error_buf, sizeof(vm_error_buf), "%p-%p %zu %s/%s\n",
		 start, end, VM_PAGE_SIZE, VM_PROT_STRING(region->prot),
		 VM_PROT_STRING(region->max_prot));

	return vm_error_buf;
}

const char *vm_region_string(struct xnd_vm_region *region)
{
	void *start, *end;

	start = region->start;
	end = (void *)((char *)region->start + region->size);

	snprintf(vm_error_buf, sizeof(vm_error_buf), "%p-%p %zu %s/%s\n",
		 start, end, region->size, VM_PROT_STRING(region->prot),
		 VM_PROT_STRING(region->max_prot));

	return vm_error_buf;
}
