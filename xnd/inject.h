/* inject.h */
#ifndef XND_INJECT_H 
#define XND_INJECT_H

struct __interpose_s {
        const void *__new_fn;
        const void *__old_fn;
};

#define INTERPOSE(__new, __old)                                 \
        static const struct __interpose_s __interpose_##__old   \
        __attribute__((used, section("__DATA,__interpose"))) =  \
        {                                                       \
                .__new_fn = (const void *)(uintptr_t)&(__new),  \
                .__old_fn = (const void *)(uintptr_t)&(__old)   \
        };

#endif /* XND_INJECT_H */
