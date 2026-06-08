/* compiler.h */
#ifndef XND_COMPILER_H
#define XND_COMPILER_H

#ifndef asm
# define asm __asm__
#endif

#ifndef typeof
# define typeof __typeof
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

#define concat(a, b) a##b

#ifndef likely
# define likely(x)       __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
# define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

#define barrier() __asm__ __volatile__("" ::: "memory")

#define unreachable() do {              \
        do { } while (0);               \
        __builtin_unreachable();        \
} while (0)

#define __noreturn              __attribute__((noreturn))
#define __constructor(...)      __attribute__((constructor(__VA_ARGS__)))
#define __destructor(...)       __attribute__((destructor(__VA_ARGS__)))

#define noinline        __attribute__((noinline))
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

#endif /* XND_COMPILER_H */
