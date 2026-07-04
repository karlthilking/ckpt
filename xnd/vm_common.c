/* vm_common.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include <mach/mach_init.h>
#include <stdio.h>

extern mach_port_t task_self_trap(void);

int ckpt_vm_protect(struct xnd_vm_region *rgn, bool max, vm_prot_t prot)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        boolean_t               set_max;
        
        addr = (mach_vm_address_t)rgn->start;
        size = (mach_vm_size_t)rgn->size;
        set_max = (boolean_t)max;

        kr = mach_vm_protect(task_self_trap(), addr, size, set_max, prot);
        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_protect(..., %d, %s): %s\n"
                          "(%p-%p %zu %s/%s, dirty pages: %u)\n"
                          "(mach_task_self(): %u, task_self_trap(): %u)\n",
                          (int)max, VM_PROT_STRING(prot),
                          mach_error_string(kr),
                          rgn->start, rgn->start + rgn->size, rgn->size,
                          VM_PROT_STRING(rgn->prot),
                          VM_PROT_STRING(rgn->max_prot),
                          rgn->pages_dirtied, mach_task_self(),
                          task_self_trap());
                return -1;
        }

        return 0;
}
