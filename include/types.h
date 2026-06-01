/* types.h */
#ifndef __TYPES_H__
#define __TYPES_H__
#define _XOPEN_SOURCE
#include <stdint.h>
#include <ucontext.h>

#if __STDC_VERSION__ < 202311L
# include <stdbool.h>
#endif

#ifndef asm
# define asm __asm__
#endif

typedef int8_t          s8, i8;
typedef int16_t         s16, i16;
typedef int32_t         s32, i32;
typedef int64_t         s64, i64;
typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef uint64_t        u64;

typedef unsigned char           uchar;
typedef unsigned short          ushort;
typedef unsigned int            uint;
typedef unsigned long           ulong, ulong32;
typedef unsigned long long      ullong, ulong64;

typedef enum    ckpt_header     ckpt_header_t;
typedef struct  ckpt_metadata   ckpt_metadata_t;
typedef struct  ckpt_vm_region  ckpt_vm_region_t;
typedef ucontext_t              ckpt_context_t;

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define barrier()       __asm__ __volatile__("" ::: "memory")

#define unreachable() do {              \
        do { } while (0);               \
        __builtin_unreachable();        \
} while (0)

#define __noreturn        __attribute__((noreturn))
#define __constructor   __attribute__((constructor))
#define __destructor    __attribute__((destructor))
#define __interpose     __attribute__((section("__DATA,__interpose")))

#define noinline        __attribute__((noinline))
#define __always_inline inline __attribute__((__always_inline__))

#endif // __TYPE_H__
