/* vm_traps.c */
#include "xnd/xnd.h"
#include "xnd/vm_region.h"
#include <mach/mach.h>

kern_return_t vm_allocate_trap(vm_map_t target, mach_vm_address_t *addr,
                                    mach_vm_size_t size, int flags)
{
        register u64 x0         __asm__("x0")   = (u64)target;
        register u64 *x1        __asm__("x1")   = (u64 *)addr;
        register u64 x2         __asm__("x2")   = (size_t)size;
        register s64 x3         __asm__("x3")   = (s64)flags;
        register s64 x16        __asm__("x16")  = (s64)-MACH_VM_ALLOCATE_TRAP;

        asm volatile(
                "svc #0x80"
                : "+r" (x0)
                : "r" (x1), "r" (x2), "r" (x3), "r" (x16)
                : "cc", "memory"
        );

        return (kern_return_t)x0;
}

kern_return_t vm_deallocate_trap(vm_map_t target, mach_vm_address_t address,
                                 mach_vm_size_t size)
{
        register u64 x0 __asm__("x0") = (u64)target;
        register u64 x1 __asm__("x1") = (u64)address;
        register u64 x2 __asm__("x2") = (u64)size;
        register s64 x16 __asm__("x16") = (s64)-MACH_VM_DEALLOCATE_TRAP;

        asm volatile(
                "svc #0x80"
                : "+r" (x0)
                : "r" (x1), "r" (x2), "r" (x16)
                : "cc", "memory"
        );

        return (kern_return_t)x0;
}
