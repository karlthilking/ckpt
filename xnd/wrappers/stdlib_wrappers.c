/* exit_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/thread_info.h"
#include "stdlib_wrappers.h"

#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>

static __always_inline bool skip_interpose(void)
{
        if (unlikely(get_xnd_state() == XND_UNINITIALIZED))
                return true;
        else if (unlikely(tlv_ok() == false))
                return true;

        return false;
}

extern void (*__cleanup)(void);

void __exit_hook(int status)
{
        if (PTRAUTH_SIGNED((uintptr_t)__cleanup)) {
                pac_strip_resign(__cleanup, APIBKey,
                                 __CLEANUP_PAC_DISCRIMINATOR, 1);
        }
        
        exit(status);
}

void __abort_hook(void)
{
        if (PTRAUTH_SIGNED((uintptr_t)__cleanup)) {
                pac_strip_resign(__cleanup, APIBKey,
                                 __CLEANUP_PAC_DISCRIMINATOR, 1);
        }

        abort();
}

void *__calloc_hook(size_t count, size_t size)
{
        void *retval;
        
        if (skip_interpose())
                return calloc(count, size);

        unsafe_enter();
        retval = calloc(count, size);
        unsafe_exit();

        return retval;
}

void __free_hook(void *ptr)
{
        if (skip_interpose()) {
                free(ptr);
                return;
        }

        unsafe_enter();
        free(ptr);
        unsafe_exit();
}

void *__malloc_hook(size_t size)
{
        void *retval;

        if (skip_interpose())
                return malloc(size);

        unsafe_enter();
        retval = malloc(size);
        unsafe_exit();

        return retval;
}

void *__realloc_hook(void *ptr, size_t size)
{
        void *retval;

        if (skip_interpose())
                return realloc(ptr, size);
        
        unsafe_enter();
        retval = realloc(ptr, size);
        unsafe_exit();

        return retval;
}

void *__reallocf_hook(void *ptr, size_t size)
{
        void *retval;

        if (skip_interpose())
                return reallocf(ptr, size);

        unsafe_enter();
        retval = reallocf(ptr, size);
        unsafe_exit();

        return retval;
}

void *__valloc_hook(size_t size)
{
        void *retval;
        
        if (skip_interpose())
                return valloc(size);

        unsafe_enter();
        retval = valloc(size);
        unsafe_exit();

        return retval;
}

void *__aligned_alloc_hook(size_t align, size_t size)
{
        void *retval;

        if (skip_interpose())
                return aligned_alloc(align, size);

        unsafe_enter();
        retval = aligned_alloc(align, size);
        unsafe_exit();

        return retval;
}

/**
 * The arc4random function of random number generator functions grab
 * internal locks that use mach ports to establish lock ownership.
 * Thus, a checkpoint should be delayed when a user thread is potentially
 * holding one of these internal locks.
 */
u32 __arc4random_hook(void)
{
        u32 retval;
        
        if (skip_interpose())
                return arc4random();

        unsafe_enter();
        retval = arc4random();
        unsafe_exit();

        return retval;
}

void __arc4random_buf_hook(void *buf, size_t nbyte)
{
        if (skip_interpose()) {
                arc4random_buf(buf, nbyte);
                return;
        }
        
        unsafe_enter();
        arc4random_buf(buf, nbyte);
        unsafe_exit();
}

u32 __arc4random_uniform_hook(u32 upper)
{
        u32 retval;
        
        if (skip_interpose())
                return arc4random_uniform(upper);

        unsafe_enter();
        retval = arc4random_uniform(upper);
        unsafe_exit();

        return retval;
}
