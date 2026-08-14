/* thread_info.h */
#ifndef XND_THREAD_INFO_H
#define XND_THREAD_INFO_H

#include <ucontext.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <sys/queue.h>

#include "xnd.h"

enum thread_state {
        ST_EMBRYO,
        ST_RUNNING,
        ST_SIGNALED,
        ST_SUSPINPROG,
        ST_SUSPENDED,
        ST_CKPT_THREAD,
        ST_UNSAFE
};

struct thread_info {
	pthread_t self;
	void *(*start_routine)(void *);
	void *arg;

	/* Current execution state */
	_Atomic enum thread_state state;
	u32 wrapper_depth;
	u8 exiting : 1,
	   joined : 1,
	   atfork : 1,
	   unused : 5;

	/* Thread stack */
	void *stackaddr;
	size_t stacksize;

	/* User context to restore */
	ucontext_t uctx;

	/* Thread-specific signal state */
	stack_t ss;
	sigset_t sigblocked;

	/* Thread-local storage */
#define TSD_LEN POSIX_THREAD_KEYS_END
#define TSD_SIZE (TSD_LEN * sizeof(void *))
#define TSD_ALIGN 16
	uintptr_t tls;
	void **tsd_keys;

	pthread_mutex_t lock;
	pthread_cond_t cond;
	TAILQ_ENTRY(thread_info) entry;
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
struct thread_info *thread_init(void *(*)(void *), void *);
struct thread_info *thread_self(void);
struct thread_info *thread_self_or_null(void);
struct thread_info *main_thread(void);

static __always_inline bool
thread_state_cas(struct thread_info *t, enum thread_state old,
		 enum thread_state new)
{
	return atomic_compare_exchange_strong(&t->state, &old, new);
}

static __always_inline void
unsafe_enter(void)
{
	struct thread_info *self = thread_self_or_null();

	if (unlikely(self == NULL))
		return;
	if (self->state == ST_EMBRYO || self->state == ST_CKPT_THREAD)
		return;
	if (self->wrapper_depth++ == 0)
		while (!thread_state_cas(self, ST_RUNNING, ST_UNSAFE));
}

static __always_inline void
unsafe_exit(void)
{
	struct thread_info *self = thread_self_or_null();

	if (unlikely(self == NULL))
		return;
	if (self->state == ST_EMBRYO || self->state == ST_CKPT_THREAD)
		return;
	if (--self->wrapper_depth == 0 &&
	    !thread_state_cas(self, ST_UNSAFE, ST_RUNNING))
		xnd_panic("internal error: unexpected thread state\n");
}

#endif /* XND_THREAD_INFO_H  */
