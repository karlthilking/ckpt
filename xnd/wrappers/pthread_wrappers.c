/* pthread_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/thread_info.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/interpose.h"
#include "xnd/util/env.h"
#include "pthread_wrappers.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <signal.h>

static __always_inline pthread_t encode_pthread(struct thread_info *th)
{
	return (pthread_t)((uintptr_t)th | PTHREAD_MAGIC);
}

static __always_inline struct thread_info *decode_pthread(pthread_t p)
{
        return (struct thread_info *)((uintptr_t)p & ~PTHREAD_TAG_MASK);
}

static __always_inline bool validate_pthread(pthread_t p)
{
        return (PTHREAD_TAG_MASK & (uintptr_t)p) == PTHREAD_MAGIC;
}

int
__pthread_create_hook(pthread_t *p, const pthread_attr_t *attr,
		      void *(*start_routine)(void *), void *arg)
{
	int err;
	struct thread_info *t;

	/*
	 * Return some reasonable error if we really failed to
	 * allocate our internal thread descriptor or failed
	 * to initialize locks/condition variables.
	 */
	unsafe_enter();
	t = thread_init(start_routine, arg);
	if (!t) {
		err = EAGAIN;
		goto out;
	}

	err = pthread_create(p, attr, thread_start, t);
	if (err) {
		free(t);
		goto out;
	}

	pthread_mutex_lock(&t->lock);
	while (t->state == ST_EMBRYO) {
		pthread_cond_wait(&t->cond, &t->lock);
	}
	pthread_mutex_unlock(&t->lock);

	/*
	 * Manipulate pthread descriptor to point to our internal
	 * thread descriptor with a magic value in the high bits
	 * for identification.
	 */
	*p = encode_pthread(t);

out:
	unsafe_exit();
	return err;
}

int
__pthread_join_hook(pthread_t p, void **value_ptr)
{
	int err;
	struct thread_info *t;

	if (!validate_pthread(p))
		return ESRCH;

	/*
	 * If the calling thread is trying to join themselves, lets
	 * handle the error independently so we don't deadlock
	 * in our own wrapper.
	 */
	t = decode_pthread(p);
	if (t == thread_self_or_null())
		return EDEADLK;

	pthread_mutex_lock(&t->lock);
	while (!t->exiting) {
		pthread_cond_wait(&t->cond, &t->lock);
	}
	pthread_mutex_unlock(&t->lock);

	/*
	 * Now that we know the thread is really exiting, we can
	 * make this join a fast, atomic operation.
	 *
	 * Only mark the thread as joined if pthread_join really
	 * succeeds. Otherwise, we might free the thread descriptor
	 * while the thread is still alive.
	 */
	unsafe_enter();
	err = pthread_join(t->self, value_ptr);
	t->joined = (err == 0);
	unsafe_exit();

	return err;
}

void __pthread_exit_hook(void *value_ptr)
{
        thread_exit(value_ptr);
}

pthread_t __pthread_self_hook(void)
{
	struct thread_info *self;

	if (xnd_tlv_ok() == false)
		return pthread_self();

	self = thread_self_or_null();
	if (!self)
		xnd_panic("internal error: thread descriptor is NULL\n");

	return encode_pthread(self);
}

int __pthread_equal_hook(pthread_t p1, pthread_t p2)
{
        if (!validate_pthread(p1) || !validate_pthread(p2)) {
                return 0;
        }

        return (uintptr_t)p1 == (uintptr_t)p2;
}

int
__pthread_kill_hook(pthread_t p, int sig)
{
	int err, ckpt_sig;
	struct thread_info *t;

	if (!validate_pthread(p))
		return ESRCH;

	ckpt_sig = env_get_ckpt_signal();
	if (sig == ckpt_sig) {
		xnd_warn("signal %d is reserved\n", ckpt_sig);
		return EINVAL;
	}

	t = decode_pthread(p);
	unsafe_enter();
	err = pthread_kill(t->self, sig);
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
        return encode_pthread(main_thread());
}

/**
 * Return non-zero if current thead is the main thread. Needs to be
 * interposed because the main thread after restart will be checkpoint
 * thread which is not really true.
 */
int __pthread_main_np_hook(void)
{
        return pthread_self() == main_thread()->self;
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
