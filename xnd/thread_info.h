/* thread_info.h */
#ifndef XND_THREAD_INFO_H
#define XND_THREAD_INFO_H

#include <mach/mach.h>
#include <ucontext.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <signal.h>
#include <sys/queue.h>

#include "xnd.h"
#include "platform/signal.h"

enum thread_state {
	TS_EMBRYO,
	TS_RUNNING,
	TS_SIGNALED,
	TS_SUSPENDING,
	TS_SUSPENDED,
	TS_UNSAFE
};

#define TSD_LEN POSIX_THREAD_KEYS_END
#define TSD_SIZE (TSD_LEN * sizeof(void *))
#define TSD_ALIGN 16

struct thread_info {
	pthread_t ti_self;
	mach_port_t ti_kport;
	void *(*ti_start)(void *);
	void *ti_arg;

	/* Current execution context/state */
	enum thread_state ti_state;
	u32 ti_depth;
	u8 ti_exiting : 1,
	   ti_joined : 1,
	   ti_atfork : 1,
	   ti_wqthread : 1,
	   ti_wqactive : 1,
	   ti_ckpt_thread : 1,
	   __unused1 : 1,
	   __unused2 : 1;

	/* Thread stack */
	void *ti_stkbottom;
	size_t ti_stksize;

	/* Saved user context */
	ucontext_t ti_uctx;

	/* Thread signal state */
	stack_t ti_sigstk;
	sigset_t ti_sigmask; /* Masked signals */
	sigset_t ti_siglist; /* Pending signals */

	/* Thread-local storage */
	uintptr_t ti_tsdbase;
	void **ti_tsdbuf;

	pthread_mutex_t ti_lock;
	pthread_cond_t ti_cond;
	TAILQ_ENTRY(thread_info) ti_entry;
};

TAILQ_HEAD(thread_list, thread_info);

void thread_list_init(void);
void thread_list_destroy(void);
void thread_list_atfork_prepare(void);
void thread_list_atfork_child(void);
void thread_list_atfork_parent(void);
void thread_list_atfork_failed(void);

void *thread_start(void *) __noreturn;
void thread_exit(void *) __noreturn;
void thread_sighandler(int, siginfo_t *, void *);

struct thread_info *wqthread_init(void);
struct thread_info *thread_init(void *(*)(void *), void *);
struct thread_info *thread_self(void);
struct thread_info *thread_self_or_null(void);
struct thread_info *main_thread(void);

static inline enum thread_state
thread_state_load_relaxed(enum thread_state *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static inline enum thread_state
thread_state_load_acquire(enum thread_state *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static inline void
thread_state_store_relaxed(enum thread_state *ptr, enum thread_state val)
{
	__atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}

static inline void
thread_state_store_release(enum thread_state *ptr, enum thread_state val)
{
	__atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}

static inline bool
thread_state_cmpxchg_weak(enum thread_state *ptr,
			  enum thread_state *expected,
			  enum thread_state desired,
			  int success, int failure)
{
	return __atomic_compare_exchange_n(ptr, expected, desired, true,
					   success, failure);
}

static inline bool
thread_state_cmpxchg_strong(enum thread_state *ptr,
			    enum thread_state *expected,
			    enum thread_state desired,
			    int success, int failure)
{
	return __atomic_compare_exchange_n(ptr, expected, desired, false,
					   success, failure);
}

static inline void
unsafe_enter(void)
{
	extern void pthread_yield_np(void);

	bool ok;
	enum thread_state state;
	struct thread_info *self = thread_self_or_null();

	if (unlikely(self == NULL) || self->ti_ckpt_thread)
		return;

	state = thread_state_load_acquire(&self->ti_state);
	if (state == TS_EMBRYO)
		return;

	if (self->ti_depth++ == 0) {
		do {
			state = TS_RUNNING;
			ok = thread_state_cmpxchg_weak(
				&self->ti_state, &state, TS_UNSAFE,
				__ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
		} while (!ok);
	}
}

static inline void
unsafe_exit(void)
{
	bool ok;
	enum thread_state state;
	struct thread_info *self = thread_self_or_null();

	if (unlikely(self == NULL) || self->ti_ckpt_thread)
		return;

	state = thread_state_load_relaxed(&self->ti_state);
	if (state == TS_EMBRYO)
		return;

	if (--self->ti_depth == 0) {
		state = TS_UNSAFE;
		ok = thread_state_cmpxchg_strong(
			&self->ti_state, &state, TS_RUNNING,
			__ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
		if (unlikely(!ok))
			xnd_panic("unexpected thread state\n");
	}
}

#endif /* XND_THREAD_INFO_H  */
