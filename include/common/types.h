/* types.h */
#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>
#include <stddef.h>

#if __STDC_VERSION__ < 202311L
# include <stdbool.h>
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
typedef unsigned long           ulong;
typedef unsigned long long      ullong;

#endif /* TYPES_H */
