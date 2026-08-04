/* byte_order.h */
#ifndef XND_BYTE_ORDER_H
#define XND_BYTE_ORDER_H

#include "types.h"

#define __bswap16(x)				\
	({					\
		u16 __x = (u16)(x);		\
		(((__x & (u16)0x00FFU) << 8) |	\
		 ((__x & (u16)0xFF00U) >> 8));	\
	})

#define __bswap32(x)					\
	({						\
		u32 __x = (u32)(x);			\
		(((__x & (u32)0x000000FFUL) << 24) |	\
		 ((__x & (u32)0x0000FF00UL) <<  8) |	\
		 ((__x & (u32)0x00FF0000UL) >>  8) |	\
		 ((__x & (u32)0xFF000000UL) << 24));	\
	})

#define __bswap64(x)						\
	({							\
		u64 __x = (u64)(x);				\
		(((__x & (u64)0x00000000000000FFULL) << 56) |	\
		 ((__x & (u64)0x000000000000FF00ULL) << 40) |	\
		 ((__x & (u64)0x0000000000FF0000ULL) << 24) |	\
		 ((__x & (u64)0x00000000FF000000ULL) <<  8) |	\
		 ((__x & (u64)0x000000FF00000000ULL) >>  8) |	\
		 ((__x & (u64)0x0000FF0000000000ULL) >> 24) |	\
		 ((__x & (u64)0x00FF000000000000ULL) >> 40) |	\
		 ((__x & (u64)0xFF00000000000000ULL) >> 56));	\
	})

#if __has_builtin(__builtin_bswap16)
# define bswap16(x) (u16)__builtin_bswap16((u16)(x))
#else
# define bswap16(x) (u16)__bswap16((u16)(x))
#endif

#if __has_builtin(__builtin_bswap32)
# define bswap32(x) (u32)__builtin_bswap32((u32)(x))
#else
# define bswap32(x) (u32)__bswap32((u32)(x))
#endif

#if __has_builtin(__builtin_bswap64)
# define bswap64(x) (u64)__builtin_bswap64((u64)(x))
#else
# define bswap64(x) (u64)__bswap64((u64)(x))
#endif

#endif /* XND_BYTE_ORDER_H */
