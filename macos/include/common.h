#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>

typedef int8_t          i8;
typedef uint8_t         u8;
typedef int16_t         i16;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef int64_t         i64;
typedef uint64_t        u64;

void writeall(int, void *, size_t);
void readall(int, void *, size_t);

#endif // __COMMON_H__
