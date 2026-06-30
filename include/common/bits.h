#ifndef BITS_H
#define BITS_H

#include <stdint.h>

#define BITS_PER_LONG           (sizeof(long) * BITS_PER_BYTE)
#define BITS_PER_LONG_LONG      (sizeof(long long) * BITS_PER_BYTE)

#define BIT_MASK_UL(n)          (1ul << ((n) % BITS_PER_LONG))
#define BIT_MASK_ULL(n)         (1ull << ((n) % BITS_PER_LONG_LONG))
#define BIT_MASK_U8(n)          (UINT8_C(1) << ((n) % 8))
#define BIT_MASK_U16(n)         (UINT16_C(1) << ((n) % 16))
#define BIT_MASK_U32(n)         (UINT32_C(1) << ((n) % 32))
#define BIT_MASK_U64(n)         (UINT64_C(1) << ((n) % 64))

#define BITS_PER_BYTE           8
#define BITS_PER_TYPE(type)     (sizeof(type) * BITS_PER_BYTE)

#define test_bit(n, a)  ((a)[n / 8] & ((typeof((a)[0]))(1) << ((n) % 8)))
#define set_bit(n, a)   ((a)[n / 8] |= ((typeof((a)[0]))(1) << ((n) % 8)))
#define clear_bit(n, a) ((a)[n / 8] &= ~((typeof((a)[0]))(1) << ((n) % 8)))

#endif /* BITS_H */
