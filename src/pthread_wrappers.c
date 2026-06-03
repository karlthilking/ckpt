/* pthread_wrappers.c */
#define _XOPEN_SOURCE
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include "pthread_wrappers.h"
#include "thread_info.h"
#include "types.h"
#include "pac.h"

int __pthread_create_hook(pthread_t *p, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg)
{
        int                     retval;
        struct thread_info      *new, *self;
        uintptr_t               ptr;
         
        self = thread_self();
        new = thread_init(start_routine, arg);

        assert(thread_state_cas(self, ST_RUNNING, ST_THREAD_CREATE));
        
        retval = pthread_create(p, attr, thread_start, new);
        if (retval != 0) {
                free(new);
                return retval;
        }

        pthread_mutex_lock(&new->lock);
        while (new->state != ST_RUNNING) {
                pthread_cond_wait(&new->cond, &new->lock);
        }
        pthread_mutex_unlock(&new->lock);

        assert(thread_state_cas(self, ST_THREAD_CREATE, ST_RUNNING));
        
        /**
         * Set the pthread_t pointer to point libckpt's internal thread
         * descriptor structure for this thread in order to virtualize
         * the pthread_t. Tag the high bits of the pointer so it can
         * be validated without being dereferenced.
         */
        ptr = ((uintptr_t)new | PTHREAD_MAGIC);
        *p = (pthread_t)ptr;

        return retval;
}

int __pthread_join_hook(pthread_t p, void **value_ptr)
{
        int                     err = 0;
        uintptr_t               ptr;
        struct thread_info      *self, *th = NULL;
        
        self = thread_self();
        ptr = (uintptr_t)p;
        if ((ptr & PTHREAD_TAG_MASK) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        assert(pthread_mutex_lock(&th->lock) == 0);
        
        while (!th->exiting) {
                err = pthread_cond_wait(&th->cond, &th->lock);
                if (err) {
                        pthread_mutex_unlock(&th->lock);
                        if (pthread_kill(th->self, 0) == ESRCH) {
                                return ESRCH;
                        }
                        return EINVAL;
                }
        }

        assert(pthread_mutex_unlock(&th->lock) == 0);
        
        assert(thread_state_cas(self, ST_RUNNING, ST_THREAD_JOIN));
        err = pthread_join(th->self, value_ptr);
        assert(thread_state_cas(self, ST_THREAD_JOIN, ST_RUNNING));

        return err;
}

void __pthread_exit_hook(void *value_ptr)
{
        thread_exit(value_ptr);
        pthread_exit(value_ptr);
}

pthread_t __pthread_self_hook(void)
{
        return (pthread_t)((uintptr_t)thread_self() | PTHREAD_MAGIC);
}

int __pthread_equal_hook(pthread_t p1, pthread_t p2)
{
        uintptr_t ptr1 = (uintptr_t)p1, ptr2 = (uintptr_t)p2;

        /**
         * Validate that the pthread_t passed into pthread_equal
         * is a real tagged struct thread_info pointer from libckpt.
         */
        if ((PTHREAD_TAG_MASK & ptr1) != PTHREAD_MAGIC ||
            (PTHREAD_TAG_MASK & ptr2) != PTHREAD_MAGIC) {
                return 0;
        }

        return ptr1 == ptr2;
}

int __pthread_kill_hook(pthread_t p, int sig)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        
        if (sig == SIGUSR1 || sig == SIGUSR2) {
                fprintf(stderr, "%s is reserved for libckpt\n",
                        (sig == SIGUSR1) ? "SIGUSR1" : "SIGUSR2");
                return EINVAL;
        }
        
        return pthread_kill(th->self, sig);
}

int __pthread_detach_hook(pthread_t p)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_detach(th->self);
}

int __pthread_sigmask_hook(int how, const sigset_t *set, sigset_t *oset)
{
        if (set) {
                sigset_t cleaned = *set;

                if (sigismember(set, SIGUSR1)) {
                        fprintf(stderr, "SIGUSR1 is reserved for "
                                "libckpt and can not be masked\n");
                        sigdelset(&cleaned, SIGUSR1);
                }
                
                if (sigismember(set, SIGUSR2)) {
                        fprintf(stderr, "SIGUSR2 is reserved for "
                                "libckpt and can not be masked\n");
                        sigdelset(&cleaned, SIGUSR2);
                }

                return pthread_sigmask(how, &cleaned, oset);
        }

        return pthread_sigmask(how, set, oset);
}

int __pthread_setschedparam_hook(pthread_t p, int policy, 
                                 const struct sched_param *param)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;
        
        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_setschedparam(th->self, policy, param);
}

int __pthread_getschedparam_hook(pthread_t p, int *policy,
                                 struct sched_param *param)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_getschedparam(th->self, policy, param);
}

void *__pthread_get_stackaddr_np_hook(pthread_t p)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return (void *)(uintptr_t)ESRCH;
        }
        
        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_get_stackaddr_np(th->self);
}

size_t __pthread_get_stacksize_np_hook(pthread_t p)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return (size_t)ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_get_stacksize_np(th->self);
}

int __pthread_cancel_hook(pthread_t p)
{
        struct thread_info      *th;
        uintptr_t               ptr = (uintptr_t)p;

        if ((PTHREAD_TAG_MASK & ptr) != PTHREAD_MAGIC) {
                return ESRCH;
        }

        th = (struct thread_info *)(ptr & ~PTHREAD_TAG_MASK);
        return pthread_cancel(th->self);
}

/**
 * Return pthread_t of the main thread. Interpose so that the real
 * main thread is returned and not the checkpoint thread who will be
 * the main thread after restart.
 */
pthread_t __pthread_main_thread_np_hook(void)
{
        return (pthread_t)((uintptr_t)main_thread() | PTHREAD_MAGIC);
}

/**
 * Return non-zero if current thead is the main thread. Needs to be
 * interposed because the main thread after restart will be checkpoint
 * thread which is not really true.
 */
int __pthread_main_np_hook(void)
{
        return thread_self() == main_thread();
}

static uintptr_t pthread_xor_cookie;

void __pthread_cookie()
{
        uintptr_t tls, self, signed_ptr, slot;
        
        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        
        /**
         * Find libpthread xor cookie by signing pthread_self pointer
         * with libpthread constant discriminator xor'd with the
         * slot value (offset 0 of pthread_self pointer). Then, the
         * cookie will used be at restart to calculate the new slot value
         * that reconstructs the pthread_self signed pointer (given the
         * DB key in the new process).
         *
         * pthread_self = tls - PTHREAD_SELF_TLS_OFFSET
         * slot = pthread_self[0]
         * signed_ptr = pacdb(pthread_self, PTHREAD_SELF_DISCRIMINATOR)
         *
         * pthread_xor_cookie = slot ^ signed_ptr
         */

        self            = tls + PTHREAD_SELF_TLS_OFFSET;
        slot            = *(uintptr_t *)self;
        signed_ptr      = self;

        PACDB(signed_ptr, PTHREAD_SELF_DISCRIMINATOR);
        pthread_xor_cookie = slot ^ signed_ptr;
}

void __pthread_slot_fixup()
{
        uintptr_t tls, *slot_ptr, old_ptr, new_ptr;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        
        /**
         * Find new pthread_self pointer address signed with DB key
         * and reconstruct slot value with signed_ptr ^ cookie.
         * Then, libpthread will use the slot value and cookie to
         * reconstruct the signed pointer at runtime, and because the
         * slot has been fixed-up, the autdb will succeed.
         *
         * slot_ptr     = (uintptr_t *)(tpidrro_el0 - 0xe0)
         * old_ptr      = pthread_xor_cookie ^ slot_ptr[0]
         *
         * new_ptr      = pac_strip_and_resign(old_ptr)
         * slot_ptr[0]  = new_ptr ^ pthread_xor_cookie
         */
        slot_ptr        = (uintptr_t *)(tls + PTHREAD_SELF_TLS_OFFSET);
        old_ptr         = pthread_xor_cookie ^ slot_ptr[0];

        new_ptr = old_ptr;
        XPACD(new_ptr);
        PACDB(new_ptr, PTHREAD_SELF_DISCRIMINATOR);

        slot_ptr[0] = new_ptr ^ pthread_xor_cookie;
}
