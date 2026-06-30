/* mach_trap.c */
#include "xnd/xnd.h"
#include <mach/mach.h>

#define VM_ALLOCATE_TRAP        10
#define VM_DEALLOCATE_TRAP      12
#define VM_PROTECT_TRAP         14
#define VM_MAP_TRAP             15
#define THREAD_SELF_TRAP        27
#define TASK_SELF_TRAP          28

extern mach_port_t thread_self_trap(void);
extern mach_port_t task_self_trap(void);

kern_return_t __vm_allocate_trap(vm_map_t target, mach_vm_address_t *addr,
                                 mach_vm_size_t size, int flags)
{
        register u64 x0         __asm__("x0")   = (u64)target;
        register u64 *x1        __asm__("x1")   = (u64 *)addr;
        register u64 x2         __asm__("x2")   = (u64)size;
        register s64 x3         __asm__("x3")   = (s64)flags;
        register s64 x16        __asm__("x16")  = -VM_ALLOCATE_TRAP;

        asm volatile(
                "svc #0x80"
                : "+r" (x0)
                : "r" (x1), "r" (x2), "r" (x3), "r" (x16)
                : "cc", "memory"
        );

        return (kern_return_t)x0;
}

kern_return_t __vm_deallocate_trap(vm_map_t target, mach_vm_address_t address,
                                   mach_vm_size_t size)
{
        register u64 x0         __asm__("x0")   = (u64)target;
        register u64 x1         __asm__("x1")   = (u64)address;
        register u64 x2         __asm__("x2")   = (u64)size;
        register s64 x16        __asm__("x16")  = -VM_DEALLOCATE_TRAP;

        asm volatile(
                "svc #0x80"
                : "+r" (x0)
                : "r" (x1), "r" (x2), "r" (x16)
                : "cc", "memory"
        );

        return (kern_return_t)x0;
}

kern_return_t __vm_protect_trap(vm_map_t target, mach_vm_address_t address,
                                mach_vm_size_t size, boolean_t set_maximum,
                                vm_prot_t new_prot)
{
        register u64 x0         __asm__("x0")   = (u64)target;
        register u64 x1         __asm__("x1")   = (u64)address;
        register u64 x2         __asm__("x2")   = (u64)size;
        register s64 x3         __asm__("x3")   = (s64)set_maximum;
        register s64 x4         __asm__("x4")   = (s64)new_prot;
        register s64 x16        __asm__("x16")  = -VM_PROTECT_TRAP;

        asm volatile(
                "svc #0x80"
                : "+r" (x0)
                : "r" (x1), "r" (x2), "r" (x3), "r" (x4), "r" (x16)
                : "cc", "memory"
        );

        return (kern_return_t)x0;
}
