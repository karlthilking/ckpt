#ifndef __ARM64_PAC_H__
#define __ARM64_PAC_H__

#include "common.h"

#if defined(__aarch64__) && defined(__ARM_ARCH_ISA_A64)

/* Strip PAC from a pointer to instruction address */
#define XPACI(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpaci %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Strip PAC from pointer to data address */
#define XPACD(ptr) \
        do { \
                __asm__ __volatile__ ( \
                        "xpacd %0" \
                        : "+r" (ptr) \
                        : \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with instruction A key and a chosen modifier */
#define PACIA(ptr, mod) \
        do { \
                __asm__ __volatile__ ( \
                        "pacia %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (mod) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with instruction B key and a chosen modifer */
#define PACIB(ptr, mod)
        do { \
                __asm__ __volatile__ ( \
                        "pacib %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (mod) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with data A key and a chosen modifier */
#define PACDA(ptr, mod)
        do { \
                __asm__ __volatile__ ( \
                        "pacda %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (mod) \
                        : "memory" \
                ); \
        } while (0)

/* Sign pointer with data B key and a chosen modifier */
#define PACDB(ptr, mod)
        do { \
                __asm__ __volatile__ ( \
                        "pacdb %0, %1" \
                        : "+r" (ptr) \
                        : "r"  (mod) \
                        : "memory" \
                ); \
        } while (0)

/**
 * Determine if a pointer is signed using a mask to select the
 * unused high bits given the virtual address size. Additionally,
 * although high bits will be set in a PAC signed pointer, bit
 * 55 in the virtual address is used to determine between low
 * and high addresses (i.e. 0000XXX... vs FFFFXXX...) so this
 * bit should not be set
 */
#define PACSIGNED(ptr, mask) (((ptr) & (mask)) && \
                             !((ptr) & (1ULL << 55)))

/**
 * While it is suggested that A keys are intended to be
 * process-independent (not unique per-process) and the B keys
 * are unique per-process, it has been demonstrated that this
 * could be system independent and is not a reliable assumption.
 *
 * https://stackoverflow.com/questions/78288651
 */
#define PAC_IAKEY       0x0     /* Instruction A key */
#define PAC_IBKEY       0x1     /* Instruction B key */
#define PAC_DAKEY       0x2     /* Data A key */
#define PAC_DBKEY       0x3     /* Data B key */
#define PAC_GAKEY       0x4     /* Generic key */

typedef struct __callframe_t    callframe_t;
typedef struct __callstack_t    callstack_t;

/**
 * A call frame structure keeps one integer from holding metadata:
 * +-------------------------------------------+
 * | modifier/discriminator | key | was_signed |
 * +-------------------------------------------+
 */

/* Bitmasks for call frame metadata */
#define FR_SIGN_MASK    (1)
#define FR_KEY_MASK     (((1 << 3) - 1) ^ 1)
#define FR_MOD_MASK     (~(FR_KEY_MASK | FR_SIGN_MASK))

/* Extract info from callframe metadata field */
#define FR_SIGNED(x)    (((x) & FR_SIGN_MASK))
#define FR_KEY(x)       (((x) & FR_KEY_MASK) >> 1)
#define FR_MOD(x)       (((x) & FR_MOD_MASK) >> 3)

struct __callframe_t {
        u64             fp;
        u64             lr;
        callframe_t     *next;
        u16             metadata;
};

struct __callstack_t {
        callframe_t     *top;
        u16             depth;
};

callframe_t *callframe_create(u64, u64, int, int);
void callstack_destroy(callstack_t *);
void framewalk(callstack_t *, u64 *);

#endif // __aarch64__ && __ARM_ARCH_ISA_A64
#endif // __ARM64_PAC_H
