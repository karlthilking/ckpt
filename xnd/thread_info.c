/* thread_info.c */
#include <errno.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "xnd.h"
#include "thread_info.h"
#include "xnd_lib.h"
#include "pac.h"
#include "tls.h"
#include "vm_region.h"
#include "util/env.h"
#include "util/log.h"
#include "coordinator/xnd_coord_api.h"
#include "coordinator/xnd_coord_client.h"
#include "wrappers/signal_wrappers.h"
#include "wrappers/pthread_wrappers.h"
#include "platform/ucontext/ucontext.h"

static inline void thread_list_add_unlocked(void);
static void thread_list_add(void);
static void thread_list_remove(struct thread_info *);
static inline void thread_list_acquire(void);
static inline void thread_list_release(void);

static void thread_reap(struct thread_info *);
static void thread_barrier(void);
static void *thread_restart(void *) __noreturn;

static void thread_save_tls(void);
static void thread_restore_tls(struct thread_info *);
static void thread_restore_context(void) __noreturn;
static void thread_save_sig_state(ucontext_t *);
static void thread_restore_sig_state(void);

static void ckpt_thread_init(void);
static void ckpt_thread_exit(void) __noreturn;
static void ckpt_thread_wait(void);
static void ckpt_thread_reap(void);
static void *ckpt_thread_work(void *);

static void zombie_list_init(void);
static void zombie_list_destroy(void);
static void zombie_list_filter(void);
static void zombie_list_add(struct thread_info *);
static void zombie_list_remove(struct thread_info *);
static inline void zombie_list_acquire(void);
static inline void zombie_list_release(void);

static void barrier_arrival_wait(void);
static void barrier_release(void);
static void raise_pending_signals(void);
static inline bool try_suspend_threads(int, int *);
static void suspend_threads(void);
static void restore_threads(void);
static void wait_for_exiting_threads(void);

_Thread_local struct thread_info *myself = NULL;
static struct thread_info *_main_thread = NULL;
static struct thread_info ckpt_thread = {0};

static struct thread_list thread_list;
static pthread_mutex_t thread_list_lock = PTHREAD_MUTEX_INITIALIZER;

static struct thread_list zombie_list;
static pthread_mutex_t zombie_list_lock = PTHREAD_MUTEX_INITIALIZER;

static sigset_t p_siglist;

static volatile int barrier_seq = 0;
static volatile int barrier_expected;
static volatile int barrier_arrived;

static pthread_cond_t cond_arrived = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_released = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ckpt_mtx = PTHREAD_MUTEX_INITIALIZER;

void
thread_list_init(void)
{
	TAILQ_INIT(&thread_list);
	xnd_tlv_init();

	_main_thread = thread_init(NULL, NULL);
	if (_main_thread == NULL)
		xnd_panic("failed to allocate thread descriptor\n");

	myself = _main_thread;
	myself->ti_self = pthread_self();
	myself->ti_kport = mach_thread_self();
	myself->ti_state = TS_RUNNING;
	thread_list_add_unlocked();

	/*
	 * unsafe_enter() was called by the user thread in the parent
	 * in __fork_hook, and unsafe_exit() will be called in both
	 * the parent and the child. Thus, this main thread in the
	 * child should call unsafe_enter() before it eventually reaches
	 * the unsafe_exit() call.
	 *
	 * Additionally, the main thread should set myself->atfork = 1
	 * so the checkpoint thread will know to pause until atfork
	 * handlers have finished executing.
	 */
	if (get_xnd_state() == XND_ATFORK) {
		unsafe_enter();
		myself->ti_atfork = 1;
	}

        zombie_list_init();
        ckpt_thread_init();
}

void
thread_list_destroy(void)
{
	struct thread_info *t, *next;

	xnd_assert(get_xnd_state() == XND_EXITING);
	if (myself != &ckpt_thread)
		ckpt_thread_reap();

	thread_list_acquire();
	TAILQ_FOREACH_SAFE(t, &thread_list, ti_entry, next) {
		thread_reap(t);
	}
	thread_list_release();

	zombie_list_destroy();
	xnd_tlv_fini();
}

static inline void
thread_list_acquire(void)
{
	xpthread_mutex_lock(&thread_list_lock);
}

static inline void
thread_list_release(void)
{
	xpthread_mutex_unlock(&thread_list_lock);
}

static inline void
thread_list_add_unlocked(void)
{
	xnd_assert(myself != NULL);
	TAILQ_INSERT_HEAD(&thread_list, myself, ti_entry);
}

static void
thread_list_add(void)
{
	thread_list_acquire();
	thread_list_add_unlocked();
	thread_list_release();
}

/*
 * thread_list_remove:
 *  Remove a thread from the thread list. thread_list_remove should
 *  only ever be called by the checkpoint thread, and should be
 *  called while thread_list_lock is already acquired. Joined threads
 *  are freed, while other exited threads are inserted into a zombie
 *  list until they are joined.
 */
static void
thread_list_remove(struct thread_info *t)
{
	TAILQ_REMOVE(&thread_list, t, ti_entry);
	if (t->ti_joined)
		thread_reap(t);
	else
		zombie_list_add(t);
}

/*
 * thread_list_atfork_prepare:
 *  Acquire all mutexes in the parent before fork(). All locks that
 *  will be used in the child are acquired and will later be
 *  re-initialized in the child process's atfork handler.
 */
void
thread_list_atfork_prepare(void)
{
	struct thread_info *t;

	thread_list_acquire();
	zombie_list_acquire();
	xpthread_mutex_lock(&ckpt_mtx);
	xpthread_mutex_lock(&ckpt_thread.ti_lock);

	TAILQ_FOREACH(t, &thread_list, ti_entry) {
		xpthread_mutex_lock(&t->ti_lock);
	}
	TAILQ_FOREACH(t, &zombie_list, ti_entry) {
		xpthread_mutex_lock(&t->ti_lock);
	}
}

/*
 * thread_list_atfork_child:
 *  Reset thread list to only include the main thread in the child
 *  process (caller of thread_list_atfork_child).
 *
 *  All locks acquired in thread_list_atfork_prepare are unlocked in
 *  reverse order. Thread descriptors from the parent process are
 *  destroyed. Locks and condition variables are re-initialized, and
 *  a new checkpoint thread is spawned.
 */
void
thread_list_atfork_child(void)
{
	struct thread_info *t, *next;

	/*
	 * Release all thread descriptor locks, and free each
	 * thread descriptor and any associated resources.
	 */
	TAILQ_FOREACH_SAFE(t, &thread_list, ti_entry, next) {
		xpthread_mutex_unlock(&t->ti_lock);
		thread_reap(t);
	}
	TAILQ_FOREACH_SAFE(t, &zombie_list, ti_entry, next) {
		xpthread_mutex_unlock(&t->ti_lock);
		thread_reap(t);
	}

	xpthread_mutex_unlock(&ckpt_thread.ti_lock);
	xpthread_mutex_unlock(&ckpt_mtx);
	zombie_list_release();
	thread_list_release();

	/*
	 * Initialize thread list and main thread struct, zombie list,
	 * and spawn a new checkpoint thread.
	 */
	thread_list_init();

	/*
	 * Re-initialize all statically-initialized mutexes and
	 * condition variables.
	 */
	xpthread_mutex_init(&ckpt_mtx, NULL);
	xpthread_mutex_init(&thread_list_lock, NULL);
	xpthread_mutex_init(&zombie_list_lock, NULL);
	xpthread_cond_init(&cond_arrived, NULL);
	xpthread_cond_init(&cond_released, NULL);

	/* Allow checkpoint thread to continue */
	pthread_mutex_lock(&ckpt_thread.ti_lock);
	xnd_assert(myself == _main_thread);
	myself->ti_atfork = 0;
	pthread_cond_signal(&ckpt_thread.ti_cond);
	pthread_mutex_unlock(&ckpt_thread.ti_lock);
}

/*
 * thread_list_atfork_parent:
 *  Release all locks that were acquired in thread_list_atfork_prepare.
 */
void
thread_list_atfork_parent(void)
{
	struct thread_info *t;

	TAILQ_FOREACH(t, &zombie_list, ti_entry) {
		xpthread_mutex_unlock(&t->ti_lock);
	}
	TAILQ_FOREACH(t, &thread_list, ti_entry) {
		xpthread_mutex_unlock(&t->ti_lock);
	}

	xpthread_mutex_unlock(&ckpt_thread.ti_lock);
	xpthread_mutex_unlock(&ckpt_mtx);
        zombie_list_release();
        thread_list_release();
}

void
thread_list_atfork_failed(void)
{
	struct thread_info *t;

	TAILQ_FOREACH(t, &zombie_list, ti_entry) {
		xpthread_mutex_unlock(&t->ti_lock);
	}
	TAILQ_FOREACH(t, &thread_list, ti_entry) {
		xpthread_mutex_unlock(&t->ti_lock);
	}

	xpthread_mutex_unlock(&ckpt_thread.ti_lock);
	xpthread_mutex_unlock(&ckpt_mtx);
        zombie_list_release();
        thread_list_release();
}

static void
zombie_list_init(void)
{
	TAILQ_INIT(&zombie_list);
}

static void
zombie_list_destroy(void)
{
	struct thread_info *t, *next;

	zombie_list_acquire();
	TAILQ_FOREACH_SAFE(t, &zombie_list, ti_entry, next) {
		thread_reap(t);
	}
	zombie_list_release();
}

static inline void
zombie_list_acquire(void)
{
	xpthread_mutex_lock(&zombie_list_lock);
}

static inline void
zombie_list_release(void)
{
	xpthread_mutex_unlock(&zombie_list_lock);
}

static void
zombie_list_filter(void)
{
	struct thread_info *t, *next;

	zombie_list_acquire();
	TAILQ_FOREACH_SAFE(t, &zombie_list, ti_entry, next) {
		if (t->ti_joined)
			zombie_list_remove(t);
	}
	zombie_list_release();
}

static void
zombie_list_add(struct thread_info *t)
{
	if (t->ti_joined) {
		thread_reap(t);
		return;
	}

	zombie_list_acquire();
	TAILQ_INSERT_HEAD(&zombie_list, t, ti_entry);
	zombie_list_release();
}

/*
 * zombie_list_remove:
 *  If a exited thread's thread descriptor isn't needed anymore, the
 *  zombie thread can be removed the zombie list and its resources can
 *  be freed. zombie_list_remove should only be called directly by
 *  zombie_list_filter (to scan for thread desciptors that are no longer
 *  needed), and the lock should be held by the caller.
 */
static void
zombie_list_remove(struct thread_info *t)
{
	xnd_assert(t->ti_joined);
	TAILQ_REMOVE(&zombie_list, t, ti_entry);
	thread_reap(t);
}

struct thread_info *
wqthread_init(void)
{
	extern int __pthread_workqueue_setkill(int);

	int err;
	struct thread_info *t;

	t = thread_init(NULL, NULL);
	if (t == NULL)
		xnd_panic("failed to allocate thread descriptor\n");

	/*
	 * Allow current workqueue thread to receive signals
	 * via pthread_kill.
	 */
	err = __pthread_workqueue_setkill(1);
	if (err != 0)
		xnd_panic("__pthread_workqueue_setkill: %s\n",
			  strerror(err));

	xnd_tlv_init();
	myself = t;

	myself->ti_self = pthread_self();
	myself->ti_kport = mach_thread_self();
	thread_list_add();
	thread_state_store_release(&myself->ti_state, TS_RUNNING);

	return t;
}

/*
 * thread_init:
 *  Initialize new thread with start routine and argument that were
 *  included as arguments to pthread_create.
 *
 *  thread_init should be called by the pthread_create wrapper to
 *  initialize a new thread, and the thread should added to the thread
 *  list by calling thread_list_add in the thread start routine
 *  wrapper/trampoline function.
 */
struct thread_info *
thread_init(void *(*start_routine)(void *), void *arg)
{
	int err;
	void **tsdbuf = NULL;
	struct thread_info *t = NULL;

	t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;

	t->ti_start = start_routine;
	t->ti_arg = arg;
	t->ti_state = TS_EMBRYO;

	err = posix_memalign((void **)&tsdbuf, TSD_ALIGN, TSD_SIZE);
	t->ti_tsdbuf = tsdbuf;
	if (err != 0) {
		free(t);
		return NULL;
	}

	err = pthread_mutex_init(&t->ti_lock, NULL);
	if (err) {
		free(t->ti_tsdbuf);
		free(t);
		return NULL;
	}

	err = pthread_cond_init(&t->ti_cond, NULL);
	if (err) {
		pthread_mutex_destroy(&t->ti_lock);
		free(t->ti_tsdbuf);
		free(t);
		return NULL;
	}

	return t;
}

/*
 * thread_reap:
 *  Free all resources associated with this thread. thread_reap should
 *  be called once a thread has exited and been joined by another thread.
 */
static void
thread_reap(struct thread_info *t)
{
	xpthread_mutex_destroy(&t->ti_lock);
	xpthread_cond_destroy(&t->ti_cond);
	free(t->ti_tsdbuf);
	free(t);
}

void
thread_exit(void *exit_value)
{
	xpthread_mutex_lock(&myself->ti_lock);
	xnd_tlv_fini();

	/*
	 * Signal a waiter, if any, in __pthread_join_hook. Upon
	 * t->ti_exiting being non-zero, the waiter can wake up and
	 * proceed to call the real pthread_join safely.
	 */
	myself->ti_exiting = 1;
	xpthread_cond_signal(&myself->ti_cond);
	xpthread_mutex_unlock(&myself->ti_lock);

	pthread_exit(exit_value);
	unreachable();
}

struct thread_info *
thread_self(void)
{
	if (unlikely(myself == NULL))
		xnd_panic("Thread descriptor is NULL\n");

        return myself;
}

struct thread_info *
thread_self_or_null(void)
{
        return myself;
}

struct thread_info *
main_thread(void)
{
	if (unlikely(_main_thread == NULL))
		xnd_panic("Main thread is NULL\n");

        return _main_thread;
}

static void
ckpt_thread_init(void)
{
	struct thread_info *t = &ckpt_thread;

	t->ti_ckpt_thread = 1;
	t->ti_state = TS_EMBRYO;

	xpthread_mutex_init(&t->ti_lock, NULL);
	xpthread_cond_init(&t->ti_cond, NULL);

	xpthread_mutex_lock(&t->ti_lock);
	xpthread_create(&t->ti_self, NULL, ckpt_thread_work, NULL);

	while (t->ti_state == TS_EMBRYO)
		xpthread_cond_wait(&t->ti_cond, &t->ti_lock);

	xpthread_mutex_unlock(&t->ti_lock);
}

static void
ckpt_thread_exit(void)
{
        /*
         * If thread_terminate() in ckpt_thread_reap fails, the
	 * checkpoint thread will still be alive and will call
	 * ckpt_thread_exit instead of being forcefully terminated.
	 * However, ckpt_thread_reap() will destroy all resources
	 * associated with the checkpoint thread regardless, so
	 * just exit and do nothing else here.
         */
        xnd_tlv_fini();
        pthread_exit(NULL);
        unreachable();
}

static void
ckpt_thread_wait(void)
{
	int ret;
	bool exited;

	ret = wait_for_ckpt_request_from_coord(&exited);
	if (ret != 0) {
		if (exited) {
			ckpt_thread_exit();
			unreachable();
		}
		xnd_panic("failed to receive checkpoint request\n");
	}

	set_xnd_state(XND_CKPT_PENDING);
}

static void *
ckpt_thread_work(void *arg)
{
        sigset_t set;
        static volatile bool restart;

	/*
	 * Block checkpoint signal in checkpoint thread. Additionally,
	 * block termination signals so user threads can handle them.
	 */
	set = SIGTERMSET | sigmask(env_get_ckpt_signal());
	xpthread_sigmask(SIG_BLOCK, &set, NULL);

        xnd_tlv_init();
	myself = &ckpt_thread;
	myself->ti_self = pthread_self();
	myself->ti_kport = mach_thread_self();

        /*
	 * Signal to main thread that initialization is finished
	 * and the checkpoint thread is ready to proceed.
         */
	xpthread_mutex_lock(&myself->ti_lock);
	myself->ti_state = TS_RUNNING;
	xpthread_cond_signal(&myself->ti_cond);

	/*
	 * If this is a child process handling atfork routines, park
	 * here until the main thread finishes executing atfork
	 * handlers.
	 */
	while (_main_thread->ti_atfork)
		xpthread_cond_wait(&myself->ti_cond, &myself->ti_lock);
        xpthread_mutex_unlock(&myself->ti_lock);

        restart = false;
        getcontext(&myself->ti_uctx);

        if (restart) {
                xnd_postrestart_early();

                thread_restore_tls(&ckpt_thread);
		xnd_tlv_init();

		myself = &ckpt_thread;
		myself->ti_self = pthread_self();
		myself->ti_kport = mach_thread_self();

		xnd_postrestart_late();
                restore_threads();
                barrier_arrival_wait();

                thread_restore_sig_state();

                zombie_list_filter();

		raise_pending_signals();
                barrier_release();
        }

        restart = true;
        for (;;) {
                xnd_log_ckpt_thread_info(myself);
                /*
                 * Wait until coordinator sends XND_CKPT_REQUEST, and
                 * transition from XND_RUNNING to XND_CKPTPENDING.
                 *
                 * Now that a checkpoint is pending, enter a global
		 * coordinator barrier until the coordinator
		 * responds with XND_CKPT_START.
                 */
                ckpt_thread_wait();
                enter_coord_barrier(COORD_BARRIER_PRECKPT);

                thread_save_tls();
                thread_save_sig_state(NULL);

                /*
                 * Suspend user threads and transition from XND_CKPTPENDING
                 * to XND_SUSPENDING.
                 */
                suspend_threads();
                wait_for_exiting_threads();
		zombie_list_filter();

                /*
                 * Wait for all threads to arrive at the barrier and
                 * transition from XND_SUSPENDING -> XND_CKPTINPROG.
                 */
                barrier_arrival_wait();

                xnd_precheckpoint();
		xnd_tlv_fini();
                xnd_checkpoint(&myself->ti_uctx);

                /*
                 * Checkpoint is complete, now wait in another coordinator
                 * barrier while the coordinator writes the checkpoint
                 * manifest.
                 */
                enter_coord_barrier(COORD_BARRIER_POSTCKPT);
		xnd_tlv_init();

                xnd_postcheckpoint();
                /*
                 * Release user threads
                 *  XND_CKPTINPROG -> XND_RUNNING
                 */
                barrier_release();
        }

        pthread_exit(NULL);
}

static void
ckpt_thread_reap(void)
{
        xnd_assert(myself != &ckpt_thread);
        xpthread_mutex_destroy(&ckpt_thread.ti_lock);
        xpthread_cond_destroy(&ckpt_thread.ti_cond);
}

/*
 * barrier_arrival_wait:
 *  Wait for all user threads to reach thread_barrier.
 */
void
barrier_arrival_wait(void)
{
	xpthread_mutex_lock(&ckpt_mtx);
	while (barrier_arrived < barrier_expected) {
		xpthread_cond_wait(&cond_arrived, &ckpt_mtx);
	}
	xpthread_mutex_unlock(&ckpt_mtx);
}

/*
 * barrier_release:
 *  Allow user threads to resume after checkpoint.
 */
void
barrier_release(void)
{
	xpthread_mutex_lock(&ckpt_mtx);
	barrier_seq++;
	xpthread_cond_broadcast(&cond_released);
	xpthread_mutex_unlock(&ckpt_mtx);
}

/*
 * raise_pending_signals:
 *  Raise signals that were pending for all threads, assuming that
 *  this means the signal was orignally directed to the entire
 *  process rather than individual threads. raise_pending_signals()
 *  should be called only once all threads have restored their signal
 *  masks (in thread_restore_sig_state).
 */
void
raise_pending_signals(void)
{
	int sig;
	pid_t pid = _real_getpid();

	for (sig = 1; sig < NSIG; sig++) {
		if (sigismember(&p_siglist, sig))
			kill(pid, sig);
	}
}

static inline bool
try_suspend_threads(int ckptsig, int *count)
{
	struct thread_info *t, *next;
	int err, sig, suspended = 0;
	bool hit, retry = false;
	enum thread_state state;

	thread_list_acquire();
	TAILQ_FOREACH_SAFE(t, &thread_list, ti_entry, next) {
		if (unlikely(t->ti_ckpt_thread)) {
			xnd_warn("checkpoint thread in thread list\n");
			thread_list_remove(t);
			continue;
		}

		if (t->ti_joined) {
			thread_list_remove(t);
			continue;
		} else if (t->ti_exiting) {
			continue;
		}

		sig = 0;
		state = thread_state_load_acquire(&t->ti_state);
		switch (state) {
		case TS_RUNNING:
			sig = ckptsig;
			hit = thread_state_cmpxchg_weak(
				&t->ti_state, &state, TS_SIGNALED,
				__ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
			if (!hit) {
				retry = true;
				break;
			}
			/* FALLTHROUGH */
		case TS_SIGNALED:
			/*
			 * state = TS_RUNNING -> sig = ckptsig
			 * state = TS_SIGNALED -> sig = 0
			 */
			err = pthread_kill(t->ti_self, sig);
			if (err == ESRCH) {
				thread_list_remove(t);
				continue;
			} else if (err != 0) {
				xnd_warn("pthread_kill: %s\n", strerror(err));
			}
			/* FALLTHROUGH */
		case TS_UNSAFE:
		case TS_EMBRYO:
			retry = true;
			break;
		case TS_SUSPENDED:
		case TS_SUSPENDING:
			suspended++;
			break;
		default:
			unreachable();
		}
	}
	thread_list_release();

	if (!retry)
		*count = suspended;
	return retry;
}

void
suspend_threads(void)
{
	bool retry;
	int count, ckptsig = env_get_ckpt_signal();

	set_xnd_state(XND_SUSPINPROG);
	xpthread_mutex_lock(&ckpt_mtx);
	barrier_arrived = 0;

	do {
		retry = try_suspend_threads(ckptsig, &count);
		if (retry)
			usleep(50);
	} while (retry);

	barrier_expected = count;
	xpthread_mutex_unlock(&ckpt_mtx);
}

void
restore_threads(void)
{
	sigset_t list, mask;
	struct thread_info *t;

	barrier_arrived = 0;
	barrier_expected = 0;

	xpthread_mutex_lock(&ckpt_mtx);
	thread_list_acquire();

	sigemptyset(&list);
	mask = ckpt_thread.ti_siglist;

	TAILQ_FOREACH(t, &thread_list, ti_entry) {
		sigandset(&list, &mask, &t->ti_siglist);
		mask = list;
		barrier_expected++;
		xpthread_create(&t->ti_self, NULL, thread_restart, t);
	}

	/*
	 * p_siglist is the intersection of every thread's set of
	 * pending signals, allowing us to determine which pending
	 * signals were process-directed and which were thread-directed.
	 * Thread-directed pending signals are re-raised by each thread
	 * individually, whereas process-directed signals will be
	 * re-raised by the checkpoint thread after all threads are
	 * restored.
	 */
	p_siglist = list;
	thread_list_release();
	xpthread_mutex_unlock(&ckpt_mtx);
}

void
wait_for_exiting_threads(void)
{
	int err, exiting, exited;
	struct thread_info *t, *next;

	thread_list_acquire();

	do {
		exiting = 0;
		exited = 0;

		TAILQ_FOREACH_SAFE(t, &thread_list, ti_entry, next) {
			if (t->ti_joined) {
				thread_list_remove(t);
				continue;
			}
			if (t->ti_exiting) {
				exiting++;
				err = pthread_kill(t->ti_self, 0);
				if (err == ESRCH) {
					exited++;
					thread_list_remove(t);
				}
			}
		}

		if (exiting != exited)
			usleep(50);
	} while (exiting != exited);

	thread_list_release();
}

static void
thread_barrier(void)
{
	int seq;

	xpthread_mutex_lock(&ckpt_mtx);
	seq = barrier_seq;

	if (++barrier_arrived == barrier_expected)
		xpthread_cond_signal(&cond_arrived);

	while (barrier_seq == seq)
		xpthread_cond_wait(&cond_released, &ckpt_mtx);

	xpthread_mutex_unlock(&ckpt_mtx);
}

void
thread_sighandler(int sig, siginfo_t *info, void *uctx)
{
	bool ok;
	enum thread_state expected;
        static _Thread_local volatile bool is_restart;

        xnd_assert(myself != NULL);
	if (myself->ti_ckpt_thread) {
		xnd_warn("checkpoint thread in %s\n", __func__);
		return;
	}

	expected = TS_SIGNALED;
	ok = thread_state_cmpxchg_strong(
		&myself->ti_state, &expected, TS_SUSPENDING,
		__ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
	if (!ok)
		xnd_panic("unexpected thread state change\n");

        /* Save state and transition to suspended */
        thread_save_tls();
        thread_save_sig_state((ucontext_t *)uctx);

	is_restart = false;
	getcontext(&myself->ti_uctx);
	if (is_restart)
		return;

        is_restart = true;
	xnd_tlv_fini();

	thread_state_store_release(&myself->ti_state, TS_SUSPENDED);
	__atomic_thread_fence(__ATOMIC_ACQUIRE);

	/* Wait in barrier before resuming */
        thread_barrier();
	xnd_tlv_init();

	thread_state_store_release(&myself->ti_state, TS_RUNNING);
}

void *
thread_start(void *thread)
{
	void *exit_value;

	xnd_tlv_init();
	myself = (struct thread_info *)thread;

	myself->ti_self = pthread_self();
	myself->ti_kport = mach_thread_self();
	thread_list_add();
	thread_state_store_release(&myself->ti_state, TS_RUNNING);

	/*
	 * Wake up the parent thread in __pthread_create_hook, who
	 * may have start sleeping on the child's condition variable
	 * duirng initialization.
	 */
	xpthread_mutex_lock(&myself->ti_lock);
	xpthread_cond_signal(&myself->ti_cond);
	xpthread_mutex_unlock(&myself->ti_lock);

	/*
	 * We only return from this if the thread does not manually
	 * exit via pthread_exit, but a wrapper around pthread_exit
	 * will ensure that the thread still goes through thread_exit
	 * regardless.
	 */
	exit_value = (*myself->ti_start)(myself->ti_arg);
	thread_exit(exit_value);

	unreachable();
}

static void *
thread_restart(void *thread)
{
	/*
	 * Restore tls before we do anything else. This first time
	 * we reference a thread-local, libmalloc will allocate
	 * space for the thread-local variable as might try to
	 * read thread-specific data that hasn't been restored.
	 */
	thread_restore_tls((struct thread_info *)thread);
	xnd_tlv_init();

	myself = (struct thread_info *)thread;
	myself->ti_state = TS_RUNNING;
	myself->ti_self = pthread_self();
	myself->ti_kport = mach_thread_self();

	thread_restore_sig_state();
	thread_barrier();
	thread_restore_context();

	unreachable();
}

static void
thread_save_tls(void)
{
	uintptr_t tls;

	xnd_assert(myself != NULL);
	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");

	/*
	 * User threads will restart on their old stack, so tsd
	 * keys should be copied to a heap allocated buffer (tsd_keys).
	 * On restart, keys will be copied from the buffer back to
	 * thread-local storage.
	 */
	if (!myself->ti_ckpt_thread) {
		xnd_assert(malloc_size(myself->ti_tsdbuf) >= TSD_SIZE);
		myself->ti_tsdbase = tls;
		xnd_tsd_copy(myself->ti_tsdbuf, (void **)tls);
		return;
	}

	/*
	 * [ Checkpoint thread falls through here ]
	 *  If this is the first checkpoint, the checkpoint thread
	 *  should save tls register as is. On restart, the checkpoint
	 *  thread will have a new TCB and can copy from saved
	 *  tls to the new TCB's tls.
	 *
	 *  Otherwise, this is not the checkpoint thread's first
	 *  time saving tls. If we have restarted, the checkpoint
	 *  thread's tsd base should be distinct from its old tsd
	 *  base, so it should copy from its only tls to its new
	 *  tls block.
	 *
	 *  The condition myself->ti_tsdbase != tls prevents the
	 *  checkpoint thread from copying tls when we are in
	 *  a process that has already taken a checkpoint, so the
	 *  checkpoint thread's tsd base has not changed.
	 */
	xnd_assert(myself == &ckpt_thread);
	if (myself->ti_tsdbase == 0) {
		myself->ti_tsdbase = tls;
		return;
	}

	if (myself->ti_tsdbase != tls)
		xnd_tsd_copy((void **)myself->ti_tsdbase, (void **)tls);
}

static void
thread_restore_tls(struct thread_info *t)
{
	void **src, **dst;
	uintptr_t tls;

	xnd_assert(t != NULL);
	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
	set_thread_cleanup_stack(tls, NULL);

	/*
	 * Checkpoint thread copies thread-specific data from previous
	 * tls (t->ti_tsdbase) to new thread-local storage. User threads
	 * copy from allocated buffer (t->ti_tsdbuf) to their new
	 * thread-local storage.
	 */
	dst = (void **)tls;
	src = t->ti_tsdbuf;
	if (t->ti_ckpt_thread)
		src = (void **)t->ti_tsdbase;

	xnd_tsd_copy(dst, src);
}

/*
 * thread_restore_context:
 *  Re-sign return addresses on the stack to avoid pointer
 *  authentication failures once we restore the saved user context.
 *  Then, do a manual context switch back to this thread's savd
 *  context.
 */
static void
thread_restore_context(void)
{
	u64 *fp = (u64 *)get_ucontext_fp(&myself->ti_uctx);

	ptrauth_resign_frames(fp);
	xnd_setcontext(&myself->ti_uctx);

	unreachable();
}

/*
 * thread_save_sig_state:
 *  Save thread's signal mask, alternate signal stack, and set
 *  of pending signals so this thread can recreate their signal
 *  state after restart.
 */
static void
thread_save_sig_state(ucontext_t *ucp)
{
	int err, ret, ckptsig = env_get_ckpt_signal();
	sigset_t mask;

	/*
	 * For user threads who are saving their signal state from
	 * thread_sighandler, the user context passed into the
	 * signal frame contains the signal mask that we are interested
	 * in (before the signal was received).
	 *
	 * For the checkpoint thread, thread_save_sig_state is not
	 * called from a signal context, so we should call pthread_sigmask
	 * to obtain the current signal mask.
	 */
	if (myself->ti_ckpt_thread) {
		err = pthread_sigmask(SIG_SETMASK, NULL, &mask);
		if (err != 0) {
			xnd_warn("pthread_sigmask: %s\n", strerror(err));
			sigemptyset(&myself->ti_sigmask);
		}
	} else {
		mask = ucp->uc_sigmask;
	}
	myself->ti_sigmask = mask;

	/*
	 * Verify that the checkpoint thread is masking the signal
	 * that we are using for checkpoints.
	 */
	if (myself->ti_ckpt_thread) {
		if (!sigismember(&myself->ti_sigmask, ckptsig)) {
			xnd_warn("Checkpoint signal unmasked\n");
			sigaddset(&myself->ti_sigmask, ckptsig);
		}
	}

	ret = sigaltstack(NULL, &myself->ti_sigstk);
	if (ret != 0) {
		xnd_perror("sigaltstack");
		myself->ti_sigstk.ss_flags = SS_DISABLE;
	}

	ret = sigpending(&myself->ti_siglist);
	if (ret != 0) {
		xnd_perror("sigpending");
		sigemptyset(&myself->ti_siglist);
	}
}

/*
 * thread_restore_sig_state:
 *  Restore the calling thread's saved signal mask and alternate
 *  signal stack if one was registered. Once the signal mask is
 *  restored, we can re-raise thread-direct signals that were
 *  pending at checkpoint.
 */
static void
thread_restore_sig_state(void)
{
	int err, ret, ckptsig = env_get_ckpt_signal();

	if (myself->ti_ckpt_thread) {
		if (!sigismember(&myself->ti_sigmask, ckptsig)) {
			xnd_warn("Checkpoint signal unmasked\n");
			sigaddset(&myself->ti_sigmask, ckptsig);
		}
	}

	/*
	 * sigreturn will already restore this signal mask once
	 * the calling thread returns from our signal handler, but
	 * restoring the signal mask early will allow us to re-raise
	 * pending signals.
	 */
	err = pthread_sigmask(SIG_SETMASK, &myself->ti_sigmask, NULL);
	if (err != 0) {
		xnd_warn("pthread_sigmask: %s\n", strerror(err));
		sigemptyset(&myself->ti_sigmask);
	}

	if (myself->ti_sigstk.ss_flags & SS_DISABLE)
		goto raise;

	myself->ti_sigstk.ss_flags &= ~SS_ONSTACK;
	ret = sigaltstack(&myself->ti_sigstk, NULL);
	if (ret != 0)
		xnd_warn("sigaltstack: %s\n", strerror(errno));

raise:
	/*
	 * Re-raise thread-directed pending signals. Pending signals
	 * that were determined to be process-directed (p_siglist) are
	 * skipped.
	 */
	for (int sig = 1; sig < NSIG; sig++) {
		if (sigismember(&p_siglist, sig))
			continue;
		if (sigismember(&myself->ti_siglist, sig) &&
		    sigismember(&myself->ti_sigmask, sig))
			pthread_kill(myself->ti_self, sig);
	}
}
