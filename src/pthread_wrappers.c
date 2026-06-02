/* pthread_wrappers.c */
#define _XOPEN_SOURCE
#include <stdio.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include "time_wrappers.h"
#include "pthread_wrappers.h"
#include "thread_info.h"
#include "types.h"
#include "pac.h"

static uintptr_t pthread_xor_cookie;

int __pthread_create_hook(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*start_routine)(void *), void *arg)
{
        int                     retval;
        struct thread_info      *new, *self;
         
        self    = thread_self();
        new     = thread_init(start_routine, arg);

        assert(thread_state_cas(self, ST_RUNNING, ST_THREAD_CREATE));
        retval = pthread_create(thread, attr, thread_start, new);
        assert(thread_state_cas(self, ST_THREAD_CREATE, ST_RUNNING));

        return retval;
}

int __pthread_join_hook(pthread_t p, void **value_ptr)
{
        int                     err = 0, tries = 0;
        struct thread_info      *self, *th = NULL;
        struct timespec         rqtp, rmtp;

        self = thread_self();
        rqtp.tv_sec = 0;
        rqtp.tv_nsec = 100 * NSEC_PER_MSEC;

        assert(thread_state_cas(self, ST_RUNNING, ST_THREAD_JOIN));
        while ((th = thread_list_find(p)) == NULL) {
                if ((err = pthread_kill(p, 0)) != 0) {
                        err = (err == ESRCH) ? ESRCH : EINVAL;
                        goto bad;
                }

                tries++;
                if (tries >= 35) {
                        err = ESRCH;
                        goto bad;
                }
        
                err = __nanosleep_hook(&rqtp, &rmtp);
                while (err != 0 && errno == EINTR) {
                        rqtp.tv_sec = rmtp.tv_sec;
                        rqtp.tv_nsec = rmtp.tv_nsec;
                        err = __nanosleep_hook(&rqtp, &rmtp);
                }
        }
        assert(thread_state_cas(self, ST_THREAD_JOIN, ST_RUNNING));
        
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
        
        if (value_ptr) {
                *value_ptr = th->exit_value;
        }

        return 0;
bad:
        assert(thread_state_cas(self, ST_THREAD_JOIN, ST_RUNNING));
        return err;
}

void __pthread_exit_hook(void *value_ptr)
{
        thread_exit(value_ptr);
        pthread_exit(value_ptr);
}

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

        PACDB(signed_ptr, (u64)PTHREAD_SELF_DISCRIMINATOR);
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
        PACDB(new_ptr, (u64)PTHREAD_SELF_DISCRIMINATOR);

        slot_ptr[0] = new_ptr ^ pthread_xor_cookie;
}
