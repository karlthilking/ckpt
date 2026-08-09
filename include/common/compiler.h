/* compiler.h */
#ifndef XND_COMPILER_H
#define XND_COMPILER_H

#ifndef asm
# define asm __asm__
#endif

#ifndef typeof
# define typeof __typeof
#endif

#ifndef __has_include
# define __has_include(x) (0)
#endif

#ifndef __has_builtin
# define __has_builtin(x) (0)
#endif

#define min(x, y)                       \
        ({                              \
                typeof(x) __x = (x);    \
                typeof(y) __y = (y);    \
                (void)(&__x == &__y);   \
                __x < __y ? __x : __y;  \
        })

#define max(x, y)                       \
        ({                              \
                typeof(x) __x = (x);    \
                typeof(y) __y = (y);    \
                (void)(&__x == &__y);   \
                __x > __y ? __x : __y;  \
        })

#define min_t(type, x, y)               \
        ({                              \
                type __x = (x);         \
                type __y = (y);         \
                __x < __y ? __x : __y;  \
        })

#define max_t(type, x, y)               \
        ({                              \
                type __x = (x);         \
                type __y = (y);         \
                __x > __y ? __x : __y;  \
        })

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define concat(a, b) a##b

#ifndef likely
# define likely(x)       __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
# define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

#define barrier() __asm__ __volatile__("" ::: "memory")

#if __has_builtin(__builtin_unreachable)
# define unreachable()				\
	do {					\
		do { } while (0);		\
		__builtin_unreachable();	\
	} while (0)
#else
# define unreachable() ((void)0)
#endif

#ifndef __noreturn
# define __noreturn __attribute__((noreturn))
#endif

#define __constructor(x)        __attribute__((constructor(x)))
#define __destructor(x)         __attribute__((destructor(x)))

#ifndef noinline
# define noinline __attribute__((noinline))
#endif

#define __always_inline inline __attribute__((always_inline))

#ifndef __used
# define __used __attribute__((used))
#endif

#ifndef __unused
# define __unused __attribute__((unused))
#endif

#ifndef __aligned
# define __aligned(x) __attribute__((aligned(x)))
#endif

#ifndef __hidden
# define __hidden __attribute__((visibility("hidden")))
#endif

#define __no_stack_protector __attribute__((no_stack_protector))

#endif /* XND_COMPILER_H */
