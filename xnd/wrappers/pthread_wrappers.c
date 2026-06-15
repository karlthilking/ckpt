/* pthread_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/thread_info.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "pthread_wrappers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <signal.h>

static __always_inline pthread_t encode_pthread(struct thread_info *t)
{
        return (pthread_t)((uintptr_t)t | PTHREAD_MAGIC);
}

static __always_inline struct thread_info *decode_pthread(pthread_t p)
{
        return (struct thread_info *)((uintptr_t)p & ~PTHREAD_TAG_MASK);
}

static __always_inline bool validate_pthread(pthread_t p)
{
        return (PTHREAD_TAG_MASK & (uintptr_t)p) == PTHREAD_MAGIC;
}

int __pthread_create_hook(pthread_t *p, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg)
{
        int                     retval;
        struct thread_info      *new;
         
        new = thread_init(start_routine, arg);
        
        unsafe_enter();
        retval = pthread_create(p, attr, thread_start, new);
        if (retval != 0) {
                xnd_warn("pthread_create: %s\n", strerror(retval));
                free(new);
                return retval;
        }
        
        xnd_assert(pthread_mutex_lock(&new->lock) == 0);
        while (new->state == ST_EMBRYO) {
                pthread_cond_wait(&new->cond, &new->lock);
        }
        xnd_assert(pthread_mutex_unlock(&new->lock) == 0);
        unsafe_exit();
        
        /**
         * Set pthread_t to be an opaque pointer to a libckpt internal
         * thread descriptor struct instead of the thread struct within
         * libpthread. This way, pthread_t's passed into other hook
         * functions will map to their thread struct in libckpt even
         * after pthread_t values have changed after a restart.
         *
         * encode_pthread will also tag the high bits of the value
         * so it can be validated before being dereferenced.
         */
        *p = encode_pthread(new);
        return retval;
}

int __pthread_join_hook(pthread_t p, void **value_ptr)
{
        int                     err;
        struct thread_info      *th;
        
        if (!validate_pthread(p))
                return ESRCH;
        th = decode_pthread(p);
        
        xnd_assert(pthread_mutex_lock(&th->lock) == 0);
        while (!th->exiting) {
                xnd_assert(pthread_cond_wait(&th->cond, &th->lock) == 0);
        }
        xnd_assert(pthread_mutex_unlock(&th->lock) == 0);
        
        unsafe_enter();
        err = pthread_join(th->self, value_ptr);
        th->joined = 1;
        unsafe_exit();

        return err;
}

void __pthread_exit_hook(void *value_ptr)
{
        thread_exit(value_ptr);
}

pthread_t __pthread_self_hook(void)
{
        struct thread_info *self = thread_self_or_null();

        if (unlikely(!tlv_ok())) {
                /**
                 * If thread locals have been dealloacted through _tlv_exit,
                 * (indicated by the return value of tlv_ok), just return
                 * the real pthread_t of the thread. It is likely the case
                 * that destructors are running and libraries need the
                 * real pthread_t.
                 */
                return pthread_self();
        } else if (unlikely(self == NULL)) {
                xnd_error("thread_self_or_null() returned NULL!\n");
                xnd_abort();
        }

        return encode_pthread(self);
}

int __pthread_equal_hook(pthread_t p1, pthread_t p2)
{
        /**
         * Validate that the pthread_t passed into pthread_equal
         * is a real tagged thread_info pointer from libckpt.
         */
        if (unlikely(!tlv_ok()))
                return pthread_equal(p1, p2);
        else if (!validate_pthread(p1) || !validate_pthread(p2))
                return 0;

        return (uintptr_t)p1 == (uintptr_t)p2;
}

int __pthread_kill_hook(pthread_t p, int sig)
{
        struct thread_info      *th;
        int                     err;

        if (!validate_pthread(p))
                return pthread_kill(p, sig);
        
        if (sig == SIGUSR1 || sig == SIGUSR2) {
                fprintf(stderr, "%s is reserved for libckpt\n",
                        (sig == SIGUSR1) ? "SIGUSR1" : "SIGUSR2");
                return EINVAL;
        }
        
        th = decode_pthread(p);
        /**
         * pthread_kill uses __pthread_kill internally which uses a mach 
         * port to identify the target thread, so pthread_kill is not safe 
         * to restart after a checkpoint (the mach port will be invalid 
         * by then).
         */
        unsafe_enter();
        err = pthread_kill(th->self, sig);
        unsafe_exit();

        return err;
}

int __pthread_detach_hook(pthread_t p)
{
        struct thread_info      *th;
        int                     err;

        if (!validate_pthread(p)) {
                return ESRCH;
        }
        
        th = decode_pthread(p);
        
        unsafe_enter();
        err = pthread_detach(th->self);
        unsafe_exit();

        return err;
}

int __pthread_setschedparam_hook(pthread_t p, int policy, 
                                 const struct sched_param *param)
{
        struct thread_info      *th;
        int                     err;
        
        if (!validate_pthread(p)) {
                return ESRCH;
        }

        th = decode_pthread(p);
        
        unsafe_enter();
        err = pthread_setschedparam(th->self, policy, param);
        unsafe_exit();

        return err;
}

int __pthread_getschedparam_hook(pthread_t p, int *policy,
                                 struct sched_param *param)
{
        struct thread_info      *th;
        int                     err;

        if (!validate_pthread(p)) {
                return ESRCH;
        }

        th = decode_pthread(p);
        
        unsafe_enter();
        err = pthread_getschedparam(th->self, policy, param);
        unsafe_exit();

        return err;
}

void *__pthread_get_stackaddr_np_hook(pthread_t p)
{
        struct thread_info      *th;
        void                    *retval;

        if (!validate_pthread(p)) {
                return (void *)(uintptr_t)ESRCH;
        }

        th = decode_pthread(p);
        if (th == thread_self_or_null() || th == main_thread()) {
                return pthread_get_stackaddr_np(th->self);
        }
        
        unsafe_enter();
        retval = pthread_get_stackaddr_np(th->self);
        unsafe_exit();

        return retval;
}

size_t __pthread_get_stacksize_np_hook(pthread_t p)
{
        struct thread_info      *th;
        size_t                  retval;

        if (!validate_pthread(p)) {
                return (size_t)ESRCH;
        }

        th = decode_pthread(p);
        if (th == thread_self_or_null() || th == main_thread()) {
                /**
                 * pthread_get_stacksize_np won't grab the internal
                 * thread list lock if the calling thread is the main
                 * thread or inquiring about its own stack so this
                 * will be safe.
                 */
                return pthread_get_stacksize_np(th->self);
        }
        
        unsafe_enter();
        retval = pthread_get_stacksize_np(th->self);
        unsafe_exit();
        
        return retval;
}

int __pthread_cancel_hook(pthread_t p)
{
        struct thread_info      *th;
        int                     err;

        if (!validate_pthread(p)) {
                return ESRCH;
        }

        th = decode_pthread(p);
        
        unsafe_enter();
        err = pthread_cancel(th->self);
        unsafe_exit();

        return err;
}

/**
 * Return pthread_t of the main thread. Interpose so that the real
 * main thread is returned and not the checkpoint thread who will be
 * the main thread after restart.
 */
pthread_t __pthread_main_thread_np_hook(void)
{
        /**
         * If destructors are being run, just return the real main thread's
         * pthread_t.
         */
        if (unlikely(!tlv_ok()))
                return pthread_main_thread_np();

        return encode_pthread(main_thread());
}

/**
 * Return non-zero if current thead is the main thread. Needs to be
 * interposed because the main thread after restart will be checkpoint
 * thread which is not really true.
 */
int __pthread_main_np_hook(void)
{
        if (unlikely(!tlv_ok()))
                return pthread_main_np();

        return pthread_self() == main_thread()->self;
}

/**
 * Examples of how libpthread uses ptrauth:
 *
 * void _pthread_init_signature(pthread_t thread) 
 * {
 *      ...
 *      th = ptrauth_sign_unauthenticated(
 *           th, ptrauth_key_process_dependent_data,
 *           ptrauth_string_discriminator("pthread.signature")
 *      );
 *      thread->sig = (uintptr_t)th ^ _pthread_ptr_munge_token
 *      ...
 * }
 *
 * thread->sig is the first field in struct pthread_s (long)
 * _pthread_ptr_munge_token = tsd[6] (slot 6 in thread specific data)
 *
 * Discriminator = ptrauth_string_discriminator("pthread.signature")
 * Key = APDBKey_EL1
 */

static uintptr_t pthread_xor_cookie;

void __pthread_cookie()
{
        uintptr_t tls, self, signed_ptr, slot;
        
        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        self = tls + TLS_PTHREAD_T_OFFSET;
        slot = *(uintptr_t *)self;
        
        signed_ptr = self;
        PACDB(signed_ptr, PTHREAD_SELF_DISCRIMINATOR);
        pthread_xor_cookie = slot ^ signed_ptr;
}

void __pthread_slot_fixup()
{
        uintptr_t tls, *slot_ptr, old_ptr, new_ptr;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        slot_ptr = (uintptr_t *)(tls + TLS_PTHREAD_T_OFFSET);
        old_ptr = pthread_xor_cookie ^ slot_ptr[0];

        new_ptr = old_ptr;
        XPACD(new_ptr);
        PACDB(new_ptr, PTHREAD_SELF_DISCRIMINATOR);

        slot_ptr[0] = new_ptr ^ pthread_xor_cookie;
}

INTERPOSE(__pthread_create_hook, pthread_create);
INTERPOSE(__pthread_join_hook, pthread_join);
INTERPOSE(__pthread_exit_hook, pthread_exit);
INTERPOSE(__pthread_self_hook, pthread_self);
INTERPOSE(__pthread_equal_hook, pthread_equal);
INTERPOSE(__pthread_kill_hook, pthread_kill);
INTERPOSE(__pthread_detach_hook, pthread_detach);
INTERPOSE(__pthread_setschedparam_hook, pthread_setschedparam);
INTERPOSE(__pthread_getschedparam_hook, pthread_getschedparam);
INTERPOSE(__pthread_get_stackaddr_np_hook, pthread_get_stackaddr_np);
INTERPOSE(__pthread_get_stacksize_np_hook, pthread_get_stacksize_np);
INTERPOSE(__pthread_cancel_hook, pthread_cancel);
INTERPOSE(__pthread_main_thread_np_hook, pthread_main_thread_np);
INTERPOSE(__pthread_main_np_hook, pthread_main_np);
