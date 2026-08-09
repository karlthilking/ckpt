/* interpose.h */
#ifndef XND_INTERPOSE_H
#define XND_INTERPOSE_H

struct __interpose {
	const void *__new_fn;
	const void *__old_fn;
};

#define INTERPOSE(new, old)					\
	static const struct __interpose __interpose_##old	\
	__attribute__((used, section("__DATA,__interpose"))) =	\
	{							\
		(const void *)(uintptr_t)&(new),		\
		(const void *)(uintptr_t)&(old),		\
	}

#define XND_SKIP_INTERPOSE() \
	(xnd_tlv_ok() == false || get_xnd_state() == XND_UNINITIALIZED)

#endif /* XND_INJECT_H */
