/* vm_common.c */
#include <stdio.h>
#include "vm_region.h"

int ckpt_vm_protect(const ckpt_vm_region_t *region,
                    int set_max_prot, vm_prot_t new_prot)
{
        kern_return_t ret;

        ret = mach_vm_protect(mach_task_self(),
                              (mach_vm_address_t)region->start,
                              region->size, set_max_prot, new_prot);

        if (ret != KERN_SUCCESS) {
                fprintf(stderr, "mach_vm_protect: %s\n",
                        mach_error_string(ret));
                return -1;
        }

        return 0;
}
