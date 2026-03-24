#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <mach/shared_region.h>
#include <mach-o/dyld.h>
#include <libproc.h>

// #ifdef VM_MEMORY_DYLD
// #undef VM_MEMORY_DYLD
// #endif
// 
// #ifdef VM_MEMORY_DYLD_MALLOC
// #undef VM_MEMORY_DYLD_MALLOC
// #endif
// 
// #define VM_MEMORY_DYLD          60
// #define VM_MEMORY_DYLD_MALLOC   61

typedef uint64_t        u64;

typedef struct {
        mach_vm_address_t       start;
        mach_vm_address_t       end;
        mach_vm_size_t          size;
        vm_prot_t               prot;
        unsigned int            tag;
        char                    name[128];
} mem_rgn_t;

// extern const void *_dyld_get_shared_cache_range(size_t *);
// extern const char *dyld_shared_cache_file_path(void);

int is_shared_cache_rgn(mach_vm_address_t addr)
{
//         static void     *base   = NULL;
//         static size_t   size    = 0;
// 
//         if (!base)
//                 base = _dyld_get_shared_cache_range(&size);
// 
//         if (addr >= (u64)base && addr < (u64)base + size)
//                 return 1;
        if (addr >= SHARED_REGION_BASE_ARM64 &&
            addr < SHARED_REGION_BASE_ARM64 +
                   SHARED_REGION_SIZE_ARM64)
                return 1;
        return 0;
}

void mem_rgn_name(mem_rgn_t *r)
{
        int rc = proc_regionfilename(getpid(),
                                     r->start,
                                     r->name,
                                     sizeof(r->name));
        
        if (rc > 0) {
                assert(r->name[0] != '\0');
                return;
        } else if (is_shared_cache_rgn(r->start)) {
                snprintf(r->name,
                         sizeof(r->name),
                         "[dyld_shared_cache]");
                return;
        }

        switch (r->tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_NANO:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
                snprintf(r->name,
                         sizeof(r->name),
                         "[heap]");
                return;
        case VM_MEMORY_STACK:
                snprintf(r->name,
                         sizeof(r->name),
                         "[stack]");
                return;
        case VM_MEMORY_GUARD:
                snprintf(r->name,
                         sizeof(r->name),
                         "[guard]");
                return;
        // case VM_MEMORY_DLYD:
        //         snprintf(r->name,
        //                  sizeof(r->name),
        //                  "[dyld]");
        //         return;
        // case VM_MEMORY_DLYD_MALLOC:
        //         snprintf(r->name,
        //                  sizeof(r->name),
        //                  "[dyld_heap]");
        //         return;
        case VM_MEMORY_SHARED_PMAP:
                snprintf(r->name,
                         sizeof(r->name),
                         "[shared_pmap]");
                return;
        default:
                break;
        }

        if (r->start == 0 && r->prot == VM_PROT_NONE) {
                snprintf(r->name, sizeof(r->name), "[zeropage]");
                return;
        } else {
                vm_prot_t mask = VM_PROT_READ | VM_PROT_WRITE |
                                 VM_PROT_EXECUTE;

                if ((r->prot == VM_PROT_NONE) | 
                   !(r->prot & mask)) {
                        snprintf(r->name,
                                 sizeof(r->name),
                                 "[guard]");
                        return;
                }
        }

        snprintf(r->name, sizeof(r->name), "[anonymous]");
        return;
}

void print_one_region(mem_rgn_t *r)
{
        char rwx[3];
        rwx[0] = (r->prot & VM_PROT_READ) ? 'r' : '-';
        rwx[1] = (r->prot & VM_PROT_WRITE) ? 'w' : '-';
        rwx[2] = (r->prot & VM_PROT_EXECUTE) ? 'x' : '-';
        
        mem_rgn_name(r);

        printf("%llx-%llx\t%c%c%c\t%s\n",
                r->start, r->end,
                rwx[0], rwx[1], rwx[2],
                r->name);
}

void print_memory_regions()
{
        mach_vm_address_t               addr    = 0;
        mach_vm_size_t                  size    = 0;
        natural_t                       depth   = 0;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t          count;
        kern_return_t                   kr;

        for (;;) {
                count = VM_REGION_SUBMAP_INFO_COUNT_64;
                kr = mach_vm_region_recurse(
                        mach_task_self(),
                        &addr,
                        &size,
                        &depth,
                        (vm_region_recurse_info_t)&info,
                        &count
                );

                if (kr == KERN_INVALID_ADDRESS)
                        break;
                else if (kr != KERN_SUCCESS) {
                        fprintf(stderr,
                                "mach_vm_region_recurse: %s\n",
                                mach_error_string(kr));
                        break;
                }

                if (info.is_submap) {
                        depth += 1;
                        continue;
                }

                mem_rgn_t r;

                r.start = addr;
                r.size  = size;
                r.end   = addr + size;
                r.prot  = info.protection;

                print_one_region(&r);

                addr += size;
        }
}

int main(void)
{
        print_memory_regions();
        exit(0);
}

