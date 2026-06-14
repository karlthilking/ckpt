/* stdlib_wrappers.h */
#ifndef STDLIB_WRAPPERS_H
#define STDLIB_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/inject.h"
#include <stdlib.h>

void    __exit_hook(int);
void    __abort_hook(void);
void    *__calloc_hook(size_t, size_t);
void    __free_hook(void *);
void    *__malloc_hook(size_t);
void    *__realloc_hook(void *, size_t);
void    *__reallocf_hook(void *, size_t);
void    *__valloc_hook(size_t);
void    *__aligned_alloc_hook(size_t, size_t);
u32     __arc4random_hook(void);
void    __arc4random_buf_hook(void *, size_t);
u32     __arc4random_uniform_hook(u32);

INTERPOSE(__exit_hook, exit);
INTERPOSE(__abort_hook, abort);
INTERPOSE(__calloc_hook, calloc);
INTERPOSE(__free_hook, free);
INTERPOSE(__malloc_hook, malloc);
INTERPOSE(__realloc_hook, realloc);
INTERPOSE(__reallocf_hook, reallocf);
INTERPOSE(__valloc_hook, valloc);
INTERPOSE(__aligned_alloc_hook, aligned_alloc);
INTERPOSE(__arc4random_hook, arc4random);
INTERPOSE(__arc4random_buf_hook, arc4random_buf);
INTERPOSE(__arc4random_uniform_hook, arc4random_uniform);

#endif /* STDLIB_WRAPPERS_H */
