/* vm_common.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include <mach/mach_init.h>
#include <stdio.h>

int ckpt_vm_protect(const struct xnd_vm_region *rgn, bool max, vm_prot_t prot)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        boolean_t               set_max;
        
        xnd_assert(mach_task_is_self(mach_task_self()));
        
        addr = (mach_vm_address_t)rgn->start;
        size = (mach_vm_size_t)rgn->size;
        set_max = (boolean_t)max;

        kr = mach_vm_protect(mach_task_self(), addr, size, set_max, prot);
        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_protect(..., %d, %s): %s\n"
                          "(%p-%p %zu %s/%s)\n",
                          (int)max, VM_PROT_STRING(prot),
                          mach_error_string(kr),
                          rgn->start, rgn->end, rgn->size, 
                          VM_PROT_STRING(rgn->prot),
                          VM_PROT_STRING(rgn->max_prot));
                return -1;
        }

        return 0;
}
