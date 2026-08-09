/* stdlib_wrappers.h */
#ifndef STDLIB_WRAPPERS_H
#define STDLIB_WRAPPERS_H

#include "xnd/xnd.h"
#include <stdlib.h>

/*
 * exit() and abort() both authenticate the address of a cleanup
 * routine before the routine is called through a function pointer.
 */
#define CLEANUP_PTRAUTH_DISCRIMINATOR (0x000000000000211BULL)

void __exit_hook(int);
void __abort_hook(void);
void *__calloc_hook(size_t, size_t);
void __free_hook(void *);
void *__malloc_hook(size_t);
void *__realloc_hook(void *, size_t);
void *__reallocf_hook(void *, size_t);
void *__valloc_hook(size_t);
void *__aligned_alloc_hook(size_t, size_t);
u32 __arc4random_hook(void);
void __arc4random_buf_hook(void *, size_t);
u32 __arc4random_uniform_hook(u32);

#endif /* STDLIB_WRAPPERS_H */
