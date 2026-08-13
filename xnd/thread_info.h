/* thread_info.h */
#ifndef XND_THREAD_INFO_H
#define XND_THREAD_INFO_H

#include "xnd/xnd.h"

#include <ucontext.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>

#define THREAD_FOREACH(t, list) \
	for ((t) = (list)->head; (t) != NULL; (t) = (t)->next)

#define THREAD_FOREACH_SAFE(t, next, list)			  \
	for ((t) = (list)->head, (next) = (t) ? (t)->next : NULL; \
	     (t) != NULL;					  \
	     (t) = next, (next) = (t) ? (t)->next : NULL)

enum thread_state {
        ST_EMBRYO,
        ST_RUNNING,
        ST_SIGNALED,
        ST_SUSPINPROG,
        ST_SUSPENDED,
        ST_CKPT_THREAD,
        ST_UNSAFE
};

struct thread_list {
	struct thread_info *head;
	pthread_mutex_t lock;
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
	   unused : 6;

	/* Thread stack */
	void *stackaddr;
	size_t stacksize;

	/* User context to restore */
	ucontext_t uctx;

	/* Thread-specific signal state */
	stack_t ss;
	sigset_t sigblocked;

	/* Thread-local storage */
#define TSD_KEYS_LEN PTHREAD_TSD_END
#define TSD_KEYS_SIZE (TSD_KEYS_LEN * sizeof(void *))
	uintptr_t tls;
	void **tsd_keys;

	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct thread_info *next;
	struct thread_info *prev;
};

void thread_list_init(void);
void thread_list_destroy(void);
void thread_list_acquire(void);
void thread_list_release(void);
void thread_list_add(void);
void thread_list_remove(struct thread_info *);

void thread_list_atfork_prepare(void);
void thread_list_atfork_child(void);
void thread_list_atfork_parent(void);
void thread_list_atfork_failed(void);

void zombie_list_init(void);
void zombie_list_destroy(void);
void zombie_list_acquire(void);
void zombie_list_release(void);
void zombie_list_filter(void);
void zombie_list_add(struct thread_info *);
void zombie_list_remove(struct thread_info *);

void thread_reap(struct thread_info *);
void thread_exit(void *);
struct thread_info *thread_init(void *(*)(void *), void *);
struct thread_info *thread_self(void);
struct thread_info *thread_self_or_null(void);
struct thread_info *main_thread(void);

void ckpt_thread_init(void);
void ckpt_thread_exit(void);
void ckpt_thread_wait(void);
void *ckpt_thread_work(void *);
void ckpt_thread_reap(void);

void barrier_arrival_wait(void);
void barrier_release(void);

void suspend_threads(void);
void restore_threads(void);
void wait_for_exiting_threads(void);

void thread_barrier(void);
void thread_sighandler(int, siginfo_t *, void *);
void *thread_start(void *);
void *thread_restart(void *);

void thread_save_tls(void);
void thread_restore_tls(struct thread_info *);
void thread_restore_context(void);
void thread_save_sig_state(void);
void thread_restore_sig_state(void);

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
