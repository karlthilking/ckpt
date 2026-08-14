/* xalloc.h */
#ifndef XND_XALLOC_H
#define XND_XALLOC_H

#include "compiler.h"

#define __xalloc(op, bytes, ...)				       \
	({							       \
		void *__p = op(__VA_ARGS__);			       \
		if (__p == NULL)				       \
			xnd_panic("%s failed to allocate %zu bytes\n", \
				  #op, bytes);			       \
		__p;						       \
	})

#define xmalloc(n) __xalloc(malloc, n, n)
#define xcalloc(c, n) __xalloc(calloc, c * n, c, n)
#define xrealloc(p, n) __xalloc(realloc, n, p n)
#define xaligned_alloc(a, n) __xalloc(aligned_alloc, n, a, n)

#define xposix_memalign(p, a, n)			  \
	do {						  \
		int __err = posix_memalign(p, a, n);	  \
		if (__err != 0)				  \
			xnd_panic("posix_memalign: %s\n", \
				  strerror(__err));	  \
	} while (0)

#endif /* XND_XALLOC_H */
