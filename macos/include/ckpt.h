#ifndef __CKPT_H__
#define __CKPT_H__
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

/* Integer types */
typedef int8_t          i8;
typedef uint8_t         u8;
typedef int16_t         i16;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef int64_t         i64;
typedef uint64_t        u64;

/* Checkpoint types */
typedef struct  __mem_rgn_t             mem_rgn_t;
typedef struct  __reg_ctx_t             reg_ctx_t;
typedef struct  __callframe_t           callframe_t;
typedef struct  __ckpt_metadata_t       ckpt_metadata_t;
typedef enum    __ckpt_hdr_t            ckpt_hdr_t;

/**
 * mem_rgn_t:   Representation of a memory region in the process's
 *              virtual address space
 *
 * @start:      Start address of the memory region
 * @end:        End address of the memory region
 * @size:       Size of the memory region
 * @prot:       Protections of the memory region
 * @max_prot:   Maximum protections of the memory region
 * @mode:       Shared or private
 * @tag:        Type of memory region
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

/* Check if virtual address is aligned to a page size boundary */
#define PG_ALIGNED(addr, pgsz) \
        (!((addr) & ((pgsz) - 1)))
/* Round down a virtual address to a page size aligned address */
#define PG_ALIGN_DN(addr, pgsz) \
        ((addr) & ~((pgsz) - 1))
/* Round up virtual memory segment size to multiple of page size */
#define PG_ALIGN_UP(size, pgsz) \
        (((size) + (pgsz) - 1) & ~((pgsz) - 1))

/**
 * Mach/MacOS related virtual address space constants for regions
 * that should not be saved during checkpoint
 * COMMPAGE
 * PAGEZERO
 * DYLD SHARED CACHE
 */
#define COMMPAGE_BASE           0x0000000FFFFFC000ULL
#define COMMPAGE_END            0x0000001000000000ULL
#define COMMPAGE(addr, size)    (((addr) >= COMMPAGE_BASE) && \
                                 ((addr) + (size) < COMMPAGE_END))

#define PAGEZERO_BASE           0x0
#define PAGEZERO_END            0x100000000ULL
#define PAGEZERO(addr, size)    (((addr) >= PAGEZERO_BASE) && \
                                 ((addr) + (size) < PAGEZERO_END))

#define DYLD_SH_CH_BASE        SHARED_REGION_BASE_ARM64
#define DYLD_SH_CH_END         (SHARED_REGION_BASE_ARM64 + \
                                SHARED_REGION_SIZE_ARM64)
#define DYLD_SH_CH(addr, size) (((addr) >= DYLD_SH_CH_BASE) && \
                                ((addr) + (size) < DYLD_SH_CH_END))

/**
 * callframe_t: Representation of a call frame saved on the
 *              process's stack
 *
 * @fp:         Saved frame pointer in the frame record that
 *              indicates the address of the previous frame record
 * @lr:         Saved link register in the frame record
 * @metadata:   Metadata indicating if registers in the frame
 *              record were signed and how
 * Structure of the saved metadata:
 * +------------------------+-----+------------+
 * | modifier/discriminator | key | was_signed |
 * +------------------------+-----+------------+
 */
struct __callframe_t {
        u64             fp;
        u64             lr;
        u16             metadata;
};

/* Bit masks for each portion of the metadata field */
#define FR_SIGN_MASK    (1)
#define FR_KEY_MASK     (((1ULL << 3) - 1) ^ 1)
#define FR_MOD_MASK     (~(FR_KEY_MASK | FR_SIGN_MASK))

/* Macros to extract each subfield from a callframe's metadata */
#define FR_SIGNED(cf)   (((cf.metadata) & FR_SIGN_MASK))
#define FR_KEY(cf)      (((cf.metadata) & FR_KEY_MASK) >> 1)
#define FR_MOD(cf)      (((cf.metadata) & FR_MOD_MASK) >> 3)

/**
 * reg_ctx_t:   Representation of a executing register context
 * 
 * @uc:         ucontext_t structure containing the values of all
 *              registers in the register context
 * @modifiers:  Array of modifiers that were used to sign any
 *              register, iff the given register was signed
 * @pac_bitmap: Bitmap indicating which registers are PAC-signed
 */
struct __reg_ctx_t {
        ucontext_t      uc;
        u8              modifiers[33];
        u64             pac_bitmap;
};

/**
 * ckpt_metadata_t: Metadata written to the start of a checkpoint
 *                  file describing the expected contents when
 *                  reading the checkpoint image
 * 
 * @nr_hdrs:        Total number of checkpoint headers
 * @nr_rgns:        Total number of memory regions
 * @nr_ctxs:        Total number of register contexts
 * @nr_frames:      Total number of annotated callframes
 */
struct __ckpt_metadata_t {
        u32 nr_hdrs;
        u32 nr_rgns;
        u32 nr_ctxs;
        u32 nr_frames;
};

/** 
 * ckpt_hdr_t:  Header that is present before each saved segment of
 *              data in a checkpoint file, indicating the type of
 *              data following any header
 */
enum __ckpt_hdr_t {
        MEM_RGN_DATA    = 0,
        REG_CTX_DATA    = 1,
        CALLFRAME_DATA  = 2
};

#define MAX_MEM_RGNS    128
#define MAX_CKPT_HDRS   256
#define MAX_CALL_FRAMES 128

/* Functions for enumerating and obtaining memory regions */
int mem_rgn_valid(vm_region_submap_info_data_64_t *,
                  mach_vm_address_t, mach_vm_size_t);
int get_mem_rgns(mem_rgn_t *);

/* Functions for reading and writing checkpoint file data */
int write_data(int, void *, size_t);
int write_ckpt(int, int, int, int,
               ckpt_hdr_t *, mem_rgn_t *,
               reg_ctx_t *, callframe_t *);

int read_data(int, void *, size_t);
int read_ckpt(int, int, int, int, int,
              ckpt_hdr_t *, mem_rgn_t *,
              reg_ctx_t *, callframe_t *);

/* Handle PAC signatures for register context */
void strip_regs(reg_ctx_t *);
void resign_regs(reg_ctx_t *);

/* Handle PAC signatures in call stack */
int strip_frames(callframe_t *, u64 *);
void resign_frames(callframe_t *, int, u64 *);

/* Number of general purpose registers on arm64 */
#define NGPREGS 29
/* Number of total registers (excluding simd, neon, fp, etc.) */
#define NREGS   33

/* arm64 gp registers and fp, lr, sp, pc */
enum {
        X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11,
        X12, X13, X14, X15, X16, X17, X18, X19, X20, X21,
        X22, X23, X24, X25, X26, X27, X28, FP, LR, SP, PC
};

/**
 * Note: It is suggested that A keys are intended to be
 * process-independent (not unique per-process) and B keys are
 * process-dependent (unique per-process), but this may not be a
 * reliable assumption to make.
 *
 * https://stackoverflow.com/questions/78288651
 */
enum {
        PAC_IAKEY,      /* Instruction A key */
        PAC_IBKEY,      /* Instruction B key */
        PAC_DAKEY,      /* Data A key */
        PAC_DBKEY,      /* Data B key */
        PAC_GAKEY       /* Generic key */
};

/**
 * Determine if a pointer was signed using a mask to select the
 * unused high bits for a given virtual address size.
 *
 * Bit 55 should not be set in a PAC signed pointer as it should
 * be left to determine between low and high addresses; i.e.
 * 0x000... vs 0xFFF...
 */
#define PACSIGNED(ptr, mask)    (((ptr) & (mask)) && \
                                !((ptr) & (1ULL << 55)))

/* Strip PAC from a pointer with an instruction address */
#define XPACI(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpaci %0":"+r"(ptr)::"memory" \
                ); \
        } while (0)

/* Strip PAC from pointer with a data address */
#define XPACD(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpacd %0":"+r"(ptr)::"memory" \
                ); \
        } while (0)

/* Sign pointer using instruction A key and a modifier */
#define PACIA(ptr, mod) \
        do { \
                __asm__ __volatile__ ( \
                        "pacia %0,%1":"+r"(ptr):"r"(mod):"memory" \
                ); \
        } while (0)

/* Sign pointer using instruction B key and a modifier */
#define PACIB(ptr, mod) \
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0,%1":"+r"(ptr):"r"(mod):"memory" \
                ); \
        } while (0)

/* Sign pointer using data A key and a chosen modifier */
#define PACDA(ptr, mod) \
        do { \
                __asm__ __volatile__ ( \
                        "pacda %0,%1":"+r"(ptr):"r"(mod):"memory" \
                ); \
        } while (0)

/* Sign pointer using data B key and a chosen modifier */
#define PACDB(ptr, mod) \
        do { \
                __asm__ __volatile__ ( \
                        "pacdb %0,%1":"+r"(ptr):"r"(mod):"memory" \
                ); \
        } while (0)

#endif // __CKPT_H__
