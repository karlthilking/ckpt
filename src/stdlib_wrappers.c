/* exit_wrappers.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "ckpt.h"
#include "pac.h"
#include "thread_info.h"
#include "stdlib_wrappers.h"
#include "tls.h"

extern void (*__cleanup)(void);

void __exit_hook(int status)
{
        if (PTRAUTH_SIGNED((uintptr_t)__cleanup)) {
                pac_strip_resign(__cleanup, APIBKey, 0x211b, 1);
        }
        
        exit(status);
}

void __abort_hook(void)
{
        if (PTRAUTH_SIGNED((u64)__cleanup)) {
                pac_strip_resign(__cleanup, APIBKey, 0x211b, 1);
        }

        abort();
}

void *__calloc_hook(size_t count, size_t size)
{
        struct thread_info      *self;
        void                    *retval;
        
        if (skip_interpose())
                return calloc(count, size);

        self = thread_self();
        unsafe_enter(self);
        retval = calloc(count, size);
        assert(unsafe_exit(self));

        return retval;
}

void __free_hook(void *ptr)
{
        struct thread_info *self;
        
        if (skip_interpose()) {
                free(ptr);
                return;
        }

        self = thread_self();
        unsafe_enter(self);
        free(ptr);
        unsafe_exit(self);
}

void *__malloc_hook(size_t size)
{
        struct thread_info  *self;
        void                *retval;

        if (skip_interpose())
                return malloc(size);

        self = thread_self();
        unsafe_enter(self);
        retval = malloc(size);
        unsafe_exit(self);

        return retval;
}

void *__realloc_hook(void *ptr, size_t size)
{
        struct thread_info      *self;
        void                    *retval;

        if (skip_interpose())
                return realloc(ptr, size);
        
        self = thread_self();
        unsafe_enter(self);
        retval = realloc(ptr, size);
        unsafe_exit(self);

        return retval;
}

void *__reallocf_hook(void *ptr, size_t size)
{
        struct thread_info      *self;
        void                    *retval;

        if (skip_interpose())
                return reallocf(ptr, size);

        self = thread_self();
        unsafe_enter(self);
        retval = reallocf(ptr, size);
        unsafe_exit(self);

        return retval;
}

void *__valloc_hook(size_t size)
{
        struct thread_info      *self;
        void                    *retval;
        
        if (skip_interpose())
                return valloc(size);

        self = thread_self();
        unsafe_enter(self);
        retval = valloc(size);
        unsafe_exit(self);

        return retval;
}

void *__aligned_alloc_hook(size_t align, size_t size)
{
        struct thread_info      *self;
        void                    *retval;

        if (skip_interpose())
                return aligned_alloc(align, size);

        self = thread_self();
        unsafe_enter(self);
        retval = aligned_alloc(align, size);
        unsafe_exit(self);

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
        struct thread_info      *self;
        u32                     retval;
        
        if (get_ckpt_state() == LIBCKPT_UNINITIALIZED) {
                return arc4random();
        }

        self = thread_self();
        unsafe_enter(self);
        retval = arc4random();
        assert(unsafe_exit(self));

        return retval;
}

void __arc4random_buf_hook(void *buf, size_t nbyte)
{
        struct thread_info *self;
        
        if (get_ckpt_state() == LIBCKPT_UNINITIALIZED) {
                return arc4random_buf(buf, nbyte);
        }
        
        self = thread_self();
        unsafe_enter(self);
        arc4random_buf(buf, nbyte);
        assert(unsafe_exit(self));
}

u32 __arc4random_uniform_hook(u32 upper)
{
        struct thread_info      *self;
        u32                     retval;
        
        if (get_ckpt_state() == LIBCKPT_UNINITIALIZED) {
                return arc4random_uniform(upper);
        }

        self = thread_self();
        unsafe_enter(self);
        retval = arc4random_uniform(upper);
        assert(unsafe_exit(self));

        return retval;
}
