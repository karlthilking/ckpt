#ifndef CKPT_H
#define CKPT_H
#include <stdlib.h>
#include <stdint.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>

typedef int8_t          i8;
typedef uint8_t         u8;
typedef int16_t         i16;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef int64_t         i64;
typedef uint64_t        u64;

/**
 * Strip PAC from high bits of pointer holding an instruction
 * address signed with either the IA or IB key.
 */
#define PACSTRIP_I(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpaci %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/**
 * Strip PAC from high bits of pointer holding a data address
 * signed with either the DA or DB key.
 */
#define PACSTRIP_D(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpacd %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/**
 * Re-sign pointer holding an instruction address using the
 * instruction A key and a selected modifier.
 */
#define PACSIGN_IA(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacia %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/**
 * Re-sign pointer holding an instruction address using the
 * instruction B key and a selected modifier.
 */
#define PACSIGN_IB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/**
 * Determine if a pointer was signed using PAC based on the
 * size of the virtual address space. The mask should contain
 * the unused high bits for the given virtual address space.
 * If the number of bits used for a virtual address space was
 * not determined accurately however, the result of this macro
 * should be a suggestion rather that the ground truth.
 */
#define PACSIGNED(ptr, mask) ((ptr) & (mask))

/**
 * ckpt_hdr_t:  Structure representing a single checkpoint header
 *              to be serialized to a checkpoint file. One header
 *              either proceeds a memory region or a register
 *              context.
 * 
 * @rgn:        Memory region associated with this header, if it
 *              is a header for a memory region.
 *
 * @uc:         ucontext_t structure associated with this header,
 *              if it is a header for a register context.
 * 
 * @type:       The type of header, either for a memory region or
 *              a register context
 */
typedef struct __ckpt_hdr_t {
        union {
                mem_rgn_t       rgn;
                ucontext_t      uc;
        };
        int     type;
} ckpt_hdr_t;

#define MEM_RGN_HDR     0x1     // Header for a memory region
#define REG_CTX_HDR     0x2     // Header for a register context

/**
 * mem_rgn_t:   Structure representing a memory region 
 *              encountered in the virtual address space
 *
 * @start:      Start address of the memory region
 * @end:        End address of the memory region
 * @size:       Size of the memory region
 * @prot:       Protections of the memory region
 * @max_prot:   Maximum protections of the memory region
 * @tag:        
 */
typedef struct __mem_rgn_t {
        mach_vm_address_t       start;
        mach_vm_address_t       end;
        mach_vm_size_t          size;
        vm_prot_t               prot;
        vm_prot_t               max_prot;
        u32                     tag;
} mem_rgn_t;

int get_mem_rgns(mem_rgn_t *);
int write_data(int, void *, size_t);
int write_ckpt(ckpt_hdr_t *, int);

#endif // #ifndef CKPT_H
