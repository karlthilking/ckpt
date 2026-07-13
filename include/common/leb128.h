/* leb128.h */
#ifndef XND_LEB128_H
#define XND_LEB128_H
#include "types.h"

static inline u8 *encode_sleb128(u8 *p, s64 value)
{
        uchar byte;

        do {
                byte = value & 0x7F;
                if ((value >>= 7) != ((byte & 0x40) ? -1 : 0))
                        byte |= 0x80;
        } while ((*p++ = byte) & 0x80);

        return p;
}

static inline u8 *decode_sleb128(u8 *p, s64 *value)
{
        s64     result = 0, shift = 0;
        uchar   byte;

        do {
                result |= (*p & 0x7FLL) << shift;
                shift += 7;
        } while (*p++ & 0x80);
        
        byte = *(p - 1);
        if (shift < 64 && (byte & 0x40))
                result |= ((s64)(~0ULL) << shift);

        if (value)
                *value = result;

        return p;
}

static inline u8 *encode_uleb128(u8 *p, u64 value)
{
        uchar byte;

        do {
                byte = value & 0x7F;
                if ((value >>= 7) != 0)
                        byte |= 0x80;
        } while ((*p++ = byte) & 0x80);

        return p;
}

static inline u8 *decode_uleb128(u8 *p, u64 *value)
{
        u64 result = 0, shift = 0;

        do {
                result |= (*p & 0x7FULL) << shift;
                shift += 7;
        } while (*p++ & 0x80);

        if (value)
                *value = result;

        return p;
}

#endif /* XND_LEB128_H */
