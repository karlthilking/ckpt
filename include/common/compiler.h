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

#define CONCAT_X(a, b) a##b
#define CONCAT(a, b) CONCAT_X(a, b)

#define TOSTRING_X(s) #s
#define TOSTRING(s) TOSTRING_X(s)

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

#ifndef __naked
# define __naked __attribute__((naked))
#endif

#define __ATTRIBUTE_0(name) __attribute__((name))
#define __ATTRIBUTE_1(name, a1) __attribute__((name(a1)))

#define __ATTRIBUTE_NARGS_X(a, b, c, d, n, ...) n
#define __ATTRIBUTE_NARGS(...) \
	__ATTRIBUTE_NARGS_X(__VA_ARGS__, 3, 2, 1, 0,)

#define __ATTRIBUTE_DISP(a, ...) \
	CONCAT(a, __ATTRIBUTE_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define __ATTRIBUTE_DECL(name, ...) \
	__ATTRIBUTE_DISP(__ATTRIBUTE_, name, ##__VA_ARGS__)

#define __constructor(...) __ATTRIBUTE_DECL(constructor, ##__VA_ARGS__)
#define __destructor(...) __ATTRIBUTE_DECL(destructor, ##__VA_ARGS__)
#define __section(...) __ATTRIBUTE_DECL(section, ##__VA_ARGS__)

#ifndef noinline
# define noinline __attribute__((noinline))
#endif

#ifndef __always_inline
# define __always_inline inline __attribute__((always_inline))
#endif

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

#ifndef __no_stack_protector
# define __no_stack_protector __attribute__((no_stack_protector))
#endif

#endif /* XND_COMPILER_H */
