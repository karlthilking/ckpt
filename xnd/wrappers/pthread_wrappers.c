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
	if (t == NULL) {
		err = EAGAIN;
		goto out;
	}

	err = pthread_create(p, attr, thread_start, t);
	if (err) {
		free(t);
		goto out;
	}

	xpthread_mutex_lock(&t->ti_lock);
	while (t->ti_state == TS_EMBRYO) {
		xpthread_cond_wait(&t->ti_cond, &t->ti_lock);
	}
	xpthread_mutex_unlock(&t->ti_lock);

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
	 * If the calling thread is trying to join themselves, handle
	 * the error independently so we don't deadlock ourselves.
	 */
	t = decode_pthread(p);
	if (t == thread_self_or_null())
		return EDEADLK;

	xpthread_mutex_lock(&t->ti_lock);
	while (!t->ti_exiting)
		xpthread_cond_wait(&t->ti_cond, &t->ti_lock);
	xpthread_mutex_unlock(&t->ti_lock);

	/*
	 * Now that we know the thread is really exiting, we can
	 * make this join a fast, atomic operation.
	 *
	 * Only mark the thread as joined if pthread_join really
	 * succeeds. Otherwise, we might free the thread descriptor
	 * while the thread is still alive.
	 */
	unsafe_enter();
	err = pthread_join(t->ti_self, value_ptr);
	t->ti_joined = (err == 0);
	unsafe_exit();

	return err;
}

void __pthread_exit_hook(void *value_ptr)
{
        thread_exit(value_ptr);
}

pthread_t __pthread_self_hook(void)
{
	uintptr_t p;
	struct thread_info *self;

	if (!xnd_tlv_ok()) {
		p = get_tls_slot(__TSD_THREAD_SELF);
		return (pthread_t)p;
	}

	self = thread_self_or_null();
	if (self == NULL)
		xnd_panic("Thread descriptor is NULL\n");

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
	int err, ckptsig;
	struct thread_info *t;

	if (!validate_pthread(p))
		return ESRCH;

	ckptsig = env_get_ckpt_signal();
	if (sig == ckptsig) {
		xnd_warn("signal is reserved: %s\n", strsignal(ckptsig));
		return EINVAL;
	}

	t = decode_pthread(p);
	unsafe_enter();
	err = pthread_kill(t->ti_self, sig);
	unsafe_exit();

	return err;
}

int __pthread_detach_hook(pthread_t p)
{
	int err;
	struct thread_info *t;

	if (!validate_pthread(p))
		return ESRCH;

	t = decode_pthread(p);
	unsafe_enter();
	err = pthread_detach(t->ti_self);
	unsafe_exit();

	return err;
}

int
__pthread_setschedparam_hook(pthread_t p, int policy,
                             const struct sched_param *param)
{
	int err;
	struct thread_info *t;

        if (!validate_pthread(p))
                return ESRCH;

        t = decode_pthread(p);
        unsafe_enter();
        err = pthread_setschedparam(t->ti_self, policy, param);
        unsafe_exit();

        return err;
}

int
__pthread_getschedparam_hook(pthread_t p, int *policy,
                             struct sched_param *param)
{
	int err;
	struct thread_info *t;

        if (!validate_pthread(p))
                return ESRCH;

        t = decode_pthread(p);
        unsafe_enter();
        err = pthread_getschedparam(t->ti_self, policy, param);
        unsafe_exit();

        return err;
}

void *
__pthread_get_stackaddr_np_hook(pthread_t p)
{
	void *stackaddr;
	struct thread_info *t;

	if (!validate_pthread(p))
		return (void *)(uintptr_t)ESRCH;

	t = decode_pthread(p);
	if (t == thread_self_or_null() || t == main_thread())
		return pthread_get_stackaddr_np(t->ti_self);

	unsafe_enter();
	stackaddr = pthread_get_stackaddr_np(t->ti_self);
	unsafe_exit();

	return stackaddr;
}

size_t
__pthread_get_stacksize_np_hook(pthread_t p)
{
	size_t stacksize;
	struct thread_info *t;

	if (!validate_pthread(p))
		return (size_t)ESRCH;

	t = decode_pthread(p);
	if (t == thread_self_or_null() || t == main_thread())
		/*
		 * libpthread's thread list lock is not acquired
		 * if the calling thread is inquiring about its
		 * own stack
		 */
		return pthread_get_stacksize_np(t->ti_self);

        unsafe_enter();
        stacksize = pthread_get_stacksize_np(t->ti_self);
        unsafe_exit();

        return stacksize;
}

int __pthread_cancel_hook(pthread_t p)
{
	int err;
	struct thread_info *t;

        if (!validate_pthread(p))
                return ESRCH;

        t = decode_pthread(p);
        unsafe_enter();
        err = pthread_cancel(t->ti_self);
        unsafe_exit();

        return err;
}

/*
 * __pthread_main_thread_np_hook:
 *  Return encoded pointer to main thread's thread descriptor. Calling
 *  main_thread() ensures we return the real main thread and not the
 *  checkpoint thread, as the checkpoint thread will become the main
 *  thread after restart.
 */
pthread_t
__pthread_main_thread_np_hook(void)
{
        return encode_pthread(main_thread());
}

int
__pthread_main_np_hook(void)
{
	return pthread_self() == main_thread()->ti_self;
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
