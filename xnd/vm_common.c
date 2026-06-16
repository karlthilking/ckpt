/* vm_common.c */
#include "xnd/vm_region.h"
#include <stdio.h>

int ckpt_vm_protect(const struct xnd_vm_region *region,
                    int set_max, vm_prot_t new_prot)
{
        kern_return_t kr;

        kr = mach_vm_protect(mach_task_self(),
                             (mach_vm_address_t)region->start,
                             region->size, set_max, new_prot);

        if (kr != KERN_SUCCESS) {
                xnd_warn("mach_vm_protect: %s\n\t(%p-%p %zu %s/%s)\n",
                         mach_error_string(kr), region->start, region->end,
                         region->size, VM_PROT_STRING(region->prot),
                         VM_PROT_STRING(region->max_prot));
                return -1;
        }

        return 0;
}
