/* vm_region.h */
#ifndef XND_VM_REGION_H
#define XND_VM_REGION_H

#include "xnd/xnd.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <mach/shared_region.h>
#include <mach/vm_prot.h>
#include <mach/vm_inherit.h>
#include <mach/vm_behavior.h>
#include <mach/vm_param.h>

struct xnd_vm_region {
        void            *start;
        size_t          size;
        vm_inherit_t    inherit;
        int             prot;
        int             max_prot;
        u32             mode;
        u32             tag;
        uint            pages_dirtied;
};

struct xnd_vm_page {
        size_t          offset;
};

#define VM_PROT_STRING(prot) \
	(((prot) == VM_PROT_NONE) ? "---" : \
	 ((prot) == VM_PROT_READ) ? "r--" : \
	 ((prot) == VM_PROT_WRITE) ? "-w-" : \
	 ((prot) == VM_PROT_EXECUTE) ? "--x" : \
	 ((prot) == (VM_PROT_READ | VM_PROT_WRITE)) ? "rw-" :	\
	 ((prot) == (VM_PROT_READ | VM_PROT_EXECUTE)) ? "r-x" : \
	 ((prot) == (VM_PROT_WRITE | VM_PROT_EXECUTE)) ? "-wx" : \
	 ((prot) == VM_PROT_ALL) ? "rwx" : "???")

/**
 * VM_INHERIT_SHARE: share with child
 * VM_INHERIT_COPY: copy into child
 * VM_INHERIT_NONE: absent in child
 * VM_INHERIT_DONATE_COPY: copy into child and delete
 */
#define VM_INHERIT_STRING(inherit) \
        ((inherit) == VM_INHERIT_SHARE ? "VM_INHERIT_SHARE"     : \
         (inherit) == VM_INHERIT_COPY ? "VM_INHERIT_COPY"       : \
         (inherit) == VM_INHERIT_NONE ? "VM_INHERIT_NONE"       : \
         (inherit) == VM_INHERIT_DONATE_COPY ? "VM_INHERIT_DONATE_COPY" : "")

#define PAGEZERO_BASE   (0x0000000000000000ULL)
#define PAGEZERO_END    (0x0000000100000000ULL)
#define PAGEZERO(__addr, __size) \
        (((__addr) >= PAGEZERO_BASE) && \
        (((__addr) + (__size)) < PAGEZERO_END))

#define DYLD_SHARED_CACHE_BASE \
        (SHARED_REGION_BASE_ARM64)
#define DYLD_SHARED_CACHE_SIZE \
        (SHARED_REGION_SIZE_ARM64)
#define DYLD_SHARED_CACHE_END \
        (DYLD_SHARED_CACHE_BASE + DYLD_SHARED_CACHE_SIZE)
#define DYLD_SHARED_CACHE_REGION(__addr, __size) \
        (((u64)(__addr) >= (DYLD_SHARED_CACHE_BASE)) && \
        (((u64)(__addr) + (u64)(__size)) < (DYLD_SHARED_CACHE_END)))

#define VM_REGION_PRIVATE(__info) \
        ((__info)->share_mode == SM_COW || \
         (__info)->share_mode == SM_PRIVATE)

#define VM_REGION_SHARED(__info) \
        ((__info)->share_mode == SM_SHARED || \
         (__info)->share_mode == SM_TRUESHARED)

#define VM_REGION_ALIASED(__info) \
        ((__info)->share_mode == SM_PRIVATE_ALIASED || \
         (__info)->share_mode == SM_SHARED_ALIASED)

/**
 * Sentinel user_tag, and behavior and inheritance attributes
 * used to indicate the restart process's own regions so
 * that they will be ignored on checkpoint (or deallocated
 * when the restart phase finishes).
 */
#define VM_MEMORY_RESTART_STACK (240)
#define RESTART_REGION_BEHAVIOR_FLAG    VM_BEHAVIOR_RSEQNTL
#define RESTART_REGION_INHERIT_FLAG     VM_INHERIT_NONE

#if defined(XND_RESTART_BASE) && defined(XND_RESTART_END)
# define RESTART_REGION(info, addr, size)                               \
        (((uintptr_t)(addr) >= XND_RESTART_BASE &&                      \
          (uintptr_t)(addr) + (uintptr_t)(size) < XND_RESTART_END) ||   \
         ((info)->inheritance == RESTART_REGION_INHERIT_FLAG &&         \
          (info)->inheritance == RESTART_REGION_BEHAVIOR_FLAG) ||       \
         ((info)->user_tag == VM_MEMORY_RESTART_STACK))
#else
# define RESTART_REGION(info, addr, size) (0)
#endif

#define IN_VM_RANGE(ptr, start, end) \
        ((uintptr_t)(ptr) >= (uintptr_t)(start) && \
         (uintptr_t)(ptr) < (uintptr_t)(end))

#define NEEDS_REMAP_BEFORE_RESTORE(r) \
        (!(DYLD_SHARED_CACHE_REGION((r)->start, (r)->size)))

#define ONLY_RESTORE_DIRTY_PAGES(r)                         \
        (DYLD_SHARED_CACHE_REGION((r)->start, (r)->size) || \
         ((r)->tag == VM_MEMORY_MALLOC_NANO) ||             \
         ((r)->tag == VM_MEMORY_MALLOC_TINY) ||             \
         ((r)->tag == VM_MEMORY_MALLOC_SMALL) ||            \
         ((r)->tag == VM_MEMORY_MALLOC_MEDIUM) ||           \
         ((r)->tag == VM_MEMORY_MALLOC_LARGE))

#define ONLY_SAVE_DIRTY_PAGES(region) ONLY_RESTORE_DIRTY_PAGES(region)

extern vm_size_t vm_page_size;

#ifndef VM_PAGE_SIZE
# define VM_PAGE_SIZE ((size_t)vm_page_size)
#else
# undef VM_PAGE_SIZE
# define VM_PAGE_SIZE ((size_t)vm_page_size)
#endif

/* From XNU source, vm_statistic.h */
#define VM_KERN_MEMORY_NONE             0
#define VM_KERN_MEMORY_OSFMK            1
#define VM_KERN_MEMORY_BSD              2
#define VM_KERN_MEMORY_IOKIT            3
#define VM_KERN_MEMORY_LIBKERN          4
#define VM_KERN_MEMORY_OSKEXT           5
#define VM_KERN_MEMORY_KEXT             6
#define VM_KERN_MEMORY_IPC              7
#define VM_KERN_MEMORY_STACK            8
#define VM_KERN_MEMORY_CPU              9
#define VM_KERN_MEMORY_PMAP             10
#define VM_KERN_MEMORY_PTE              11
#define VM_KERN_MEMORY_ZONE             12
#define VM_KERN_MEMORY_KALLOC           13
#define VM_KERN_MEMORY_COMPRESSOR       14
#define VM_KERN_MEMORY_COMPRESSED_DATA  15
#define VM_KERN_MEMORY_PHANTOM_CACHE    16
#define VM_KERN_MEMORY_WAITQ            17
#define VM_KERN_MEMORY_DIAG             18
#define VM_KERN_MEMORY_LOG              19
#define VM_KERN_MEMORY_FILE             20
#define VM_KERN_MEMORY_MBUF             21
#define VM_KERN_MEMORY_UBC              22
#define VM_KERN_MEMORY_SECURITY         23
#define VM_KERN_MEMORY_MLOCK            24
#define VM_KERN_MEMORY_REASON           25
#define VM_KERN_MEMORY_SKYWALK          26
#define VM_KERN_MEMORY_LTABLE           27
#define VM_KERN_MEMORY_HV               28
#define VM_KERN_MEMORY_KALLOC_DATA      29
#define VM_KERN_MEMORY_RETIRED          30
#define VM_KERN_MEMORY_KALLOC_TYPE      31
#define VM_KERN_MEMORY_TRIAGE           32
#define VM_KERN_MEMORY_RECOUNT          33
#define VM_KERN_MEMORY_EXCLAVES         35
#define VM_KERN_MEMORY_EXCLAVES_SHARED  36
#define VM_KERN_MEMORY_KALLOC_SHARED    37
#define VM_KERN_MEMORY_CPUTRACE         38
#define VM_KERN_MEMORY_FIRST_DYNAMIC    39

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int ckpt_vm_mark_regions(void);
int ckpt_vm_restore_region(int, struct xnd_vm_region *);
mach_vm_address_t ckpt_vm_find_ubc_region(mach_vm_size_t *);
int ckpt_vm_remove_xnd_guard(void);

int ckpt_vm_valid_region(vm_region_submap_info_data_64_t *,
                         mach_vm_address_t, mach_vm_size_t);
u32 ckpt_vm_save_regions(struct xnd_vm_region *);
void ckpt_vm_deallocate_regions(void);

int ckpt_vm_protect(struct xnd_vm_region *, bool, vm_prot_t);
const char *vm_page_string(struct xnd_vm_region *, struct xnd_vm_page *);
const char *vm_region_string(struct xnd_vm_region *);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* XND_VM_REGION_H */
