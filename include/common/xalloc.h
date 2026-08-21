/* xalloc.h */
#ifndef XND_XALLOC_H
#define XND_XALLOC_H

#include "compiler.h"

#ifdef xnd_panic
# define xalloc_die(fmt, ...) xnd_panic(fmt, ##__VA_ARGS__)
#else
# include <stdlib.h>
# define xalloc_die(fmt, ...)			     \
	do {					     \
		fprintf(stderr, fmt, ##__VA_ARGS__); \
		exit(-1);			     \
	} while (0)
#endif

#define __xalloc(op, bytes, ...)					\
	({								\
		void *__p = op(__VA_ARGS__);				\
		if (unlikely(__p == NULL))				\
			xalloc_die("%s failed to allocate %zu bytes\n", \
				   TOSTRING(op), bytes);		\
		__p;							\
	})

#define xmalloc(size) \
	__xalloc(malloc, size, size)
#define xcalloc(count, size) \
	__xalloc(calloc, count * size, count, size)
#define xrealloc(ptr, size) \
	__xalloc(realloc, size, ptr, size)
#define xaligned_alloc(align, size) \
	__xalloc(aligned_alloc, size, align, size)

#define xposix_memalign(ptr, align, size)		      \
	do {						      \
		int __err = posix_memalign(ptr, align, size); \
		if (unlikely(__err != 0))		      \
			xalloc_die("posix_memalign: %s\n",    \
				   strerror(__err));	      \
	} while (0)

#endif /* XND_XALLOC_H */
