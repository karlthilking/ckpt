#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <mach/shared_region.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <libproc.h>
#include <dlfcn.h>
#include "../include/ckpt.h"

#define COMMPAGE_BASE           0x0000000FFFFFC000ULL
#define COMMPAGE_END            0x0000001000000000ULL
#define COMMPAGE(addr, size)    ((addr >= COMMPAGE_BASE) && \
                                 (addr + size < COMMPAGE_END))

#define PAGEZERO_BASE           0x0
#define PAGEZERO_END            0x100000000ULL
#define PAGEZERO(addr, size)    ((addr >= PAGEZERO_BASE) && \
                                 (addr + size < PAGEZERO_END))

#define DYLD_SHARED_BASE        SHARED_REGION_BASE_ARM64
#define DYLD_SHARED_END         (SHARED_REGION_BASE_ARM64 + \
                                 SHARED_REGION_SIZE_ARM64)
#define DYLD_SHARED(addr, size) ((addr >= DYLD_SHARED_BASE) && \
                                 (addr + size < DYLD_SHARED_END))
void memrgn_name(mem_rgn_t *r, char *name)
{
        Dl_info info;
        int     rc;
        
        rc = proc_regionfilename(getpid(), r->start, name, 128);
        if (rc > 0)
                return;
        
        if (COMMPAGE(r->start, r->size)) {
                snprintf(name, 128, "[commpage]");
                return;
        } else if (PAGEZERO(r->start, r->size)) {
                snprintf(name, 128, "__PAGEZERO");
                return;
        } else if (DYLD_SHARED(r->start, r->size)) {
                snprintf(name, 128, "[dyld_shared_cache]");
                return;
        } else if (dladdr((void *)r->start, &info) != 0) {
                strncpy(name, info.dli_fname,
                        strlen(info.dli_fname) + 1);
                name[strlen(info.dli_fname)] = '\0';
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
                if (r->prot == VM_PROT_NONE)
                        snprintf(name, 128, "[heap_guard]");
                else if (r->tag == VM_MEMORY_MALLOC &&
                         r->prot == (VM_PROT_READ | VM_PROT_WRITE))
                        snprintf(name, 128, "[heap_metadata]");
                else if (r->prot == VM_PROT_READ)
                        snprintf(name, 128, "[heap_checksum]");
                else
                        snprintf(name, 128, "[heap]");
                return;
        case VM_MEMORY_REALLOC:
                snprintf(name, 128, "[kmem]");
                return;
        case VM_MEMORY_STACK:
                if (r->prot == VM_PROT_NONE)
                        snprintf(name, 128, "[stack_guard]");
                else
                        snprintf(name, 128, "[stack]");
                return;
        case VM_MEMORY_GUARD:
                snprintf(name, 128, "[guard]");
                return;
        case VM_MEMORY_DYLD:
                snprintf(name, 128, "[dyld]");
                return;
        case VM_MEMORY_DYLD_MALLOC:
                snprintf(name, 128, "[dyld_heap]");
                return;
        case VM_MEMORY_DYLIB:
                snprintf(name, 128, "[dylib]");
                return;
        case VM_MEMORY_SHARED_PMAP:
                snprintf(name, 128, "[shared_pmap]");
                return;
        default:
                break;
        }

        if (r->prot == VM_PROT_NONE)
                snprintf(name, 128, "[guard]");
        else if (r->prot == (VM_PROT_READ | VM_PROT_EXECUTE))
                snprintf(name, 128, "__TEXT");
        else if (r->prot == (VM_PROT_READ | VM_PROT_WRITE))
                snprintf(name, 128, "__DATA | __BSS");
        else if (r->prot == VM_PROT_READ)
                snprintf(name, 128, "__DATA_CONST | __AUTH_CONST");
        else 
                snprintf(name, 128, "[anonymous]");
}

void prot_rwx(vm_prot_t prot, char *rwx)
{
        rwx[0] = (prot & VM_PROT_READ) ? 'r' : '-';
        rwx[1] = (prot & VM_PROT_WRITE) ? 'w' : '-';
        rwx[2] = (prot & VM_PROT_EXECUTE) ? 'x' : '-';
}

void print_memrgn(mem_rgn_t *r)
{
        char name[128];
        char rwx[3];

        prot_rwx(r->prot, rwx);
        memrgn_name(r, name);

        printf("%llu-%llu %c%c%c %s\n",
               r->start, r->end,
               rwx[0], rwx[1], rwx[2],
               name);
}

void get_memrgns()
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

                if (kr) {
                        fprintf(stderr, "mach_vm_recurse: %s\n",
                                mach_error_string(kr));
                        break;
                }

                if (info.is_submap) {
                        depth++;
                        continue;
                }

                mem_rgn_t r;

                r.start         = addr;
                r.size          = size;
                r.end           = addr + size;
                r.prot          = info.protection;
                r.max_prot      = info.max_protection;
                r.mode          = info.share_mode;
                r.tag           = info.user_tag;

                print_memrgn(&r);
                addr += size;
        }
}

int main(void)
{
        get_memrgns();
        return 0;
}
