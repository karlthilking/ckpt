/* interpose.h */
#ifndef XND_INTERPOSE_H
#define XND_INTERPOSE_H

#include "common/compiler.h"

#define INTERPOSE(new, old)					\
	static const struct {					\
		const void *__replacement;			\
		const void *__replacee;				\
	} __interpose_##old					\
	__used __section("__DATA,__interpose") = {		\
		(const void *)(uintptr_t)&(new),		\
		(const void *)(uintptr_t)&(old),		\
	}

#define XND_SKIP_INTERPOSE() \
	(xnd_tlv_ok() == false || get_xnd_state() == XND_UNINITIALIZED)

#endif /* XND_INJECT_H */
