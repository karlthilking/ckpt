#ifndef CKPT_H
#define CKPT_H
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <ucontext.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <mach/shared_region.h>
#include <mach-o/dyld.h>

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
#define XPACI(ptr) \
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
#define XPACD(ptr) \
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
#define PACIA(ptr, modifier) \
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
#define PACIB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/**
 * Re-sign pointer holding a data address using the data B key
 * and a selected modifier.
 */
#define PACDA(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacda %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (modifier) \
                        : "memory" \
                ); \
        } while (0)

/**
 * Re-sign pointer holding a data address using the data B key
 * and a selected modifier.
 */
#define PACDB(ptr, modifier) \
        do { \
                __asm__ __volatile__ ( \
                        "pacdb %0, %1" \
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

#define PACMOD_I(ptr, raw, mod) \
        do { \
                PACIB((raw), (mod)); \
        } while (0) \
        ((raw) == (ptr))

typedef struct __ckpt_hdr_t     ckpt_hdr_t;
typedef struct __mem_rgn_t      mem_rgn_t;
typedef struct __reg_ctx_t      reg_ctx_t;

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
struct __mem_rgn_t {
        mach_vm_address_t       start;
        mach_vm_address_t       end;
        mach_vm_size_t          size;
        vm_prot_t               prot;
        vm_prot_t               max_prot;
        u32                     mode;
        u32                     tag;
};

/**
 * reg_ctx_t:   Register context describing execution of a single
 *              thread before checkpointing.
 *
 * @uc:         ucontext_t structure containing the register
 *              context of one thread
 * @modifiers:  Mapping of register number to the register used
 *              as a modifier (valid iff the register number
 *              used to index the array was signed at checkpoint)
 * @pacmap:     Bitmap to indicate which registers were PAC
 *              signed at checkpoint, and thus need to be
 *              re-signed before restarting
 */
struct __reg_ctx_t {
        ucontext_t      uc;
        u8              modifiers[33];
        u64             pacmap;
};

/**
 * ckpt_hdr_t:  Structure representing a single checkpoint header
 *              to be serialized to a checkpoint file. One header
 *              either proceeds a memory region or a register
 *              context.
 * 
 * @rgn:        Memory region associated with this header, if it
 *              is a header for a memory region
 * @ctx:        The register context saved after this header, iff
 *              this is a header for a register context
 * @type:       The type of header, either for a memory region or
 *              a register context
 */
struct __ckpt_hdr_t {
        union {
                mem_rgn_t       rgn;
                reg_ctx_t       ctx;
        };
        int     type;
};

#define MAX_MEM_RGNS    100
#define MAX_CKPT_HDRS   100

#define MEM_RGN_HDR     0x1     // Header for a memory region
#define REG_CTX_HDR     0x2     // Header for a register context

#define INSTR_ADDR   0x1
#define DATA_ADDR    0x2

int mem_rgn_valid(vm_region_submap_info_data_64_t *,
                  mach_vm_address_t, mach_vm_size_t);
int get_mem_rgns(mem_rgn_t *);

int write_data(int, void *, size_t);
int write_ckpt(ckpt_hdr_t *, int);

void ckpt_handler(int);

int pac_modifier(mcontext_t, u64, u64, u8, u8 *);
void pac_strip(reg_ctx_t *);
void pac_resign(reg_ctx_t *);

#endif // #ifndef CKPT_H
