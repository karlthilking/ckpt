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
#include "common/xalloc.h"
#include "common/xthread.h"
#include "coordinator/xnd_coord_api.h"
#include "coordinator/xnd_coord_client.h"
#include "wrappers/signal_wrappers.h"
#include "wrappers/pthread_wrappers.h"
#include "platform/ucontext/ucontext.h"

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
static void thread_save_sig_state(void);
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

static sigset_t proc_siglist;

static volatile int barrier_seq = 0;
static volatile int barrier_expected;
static volatile int barrier_arrived;

static pthread_cond_t cond_arrived = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_released = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ckpt_mtx = PTHREAD_MUTEX_INITIALIZER;

void
thread_list_init(void)
{
	void **tsd_keys = NULL;

	TAILQ_INIT(&thread_list);
	xnd_tlv_init();

	_main_thread = xcalloc(1, sizeof(*_main_thread));
	xposix_memalign((void **)&tsd_keys, TSD_ALIGN, TSD_SIZE);
	_main_thread->tsd_keys = tsd_keys;
	bzero(_main_thread->tsd_keys, TSD_SIZE);

	xpthread_mutex_init(&_main_thread->lock, NULL);
	xpthread_cond_init(&_main_thread->cond, NULL);

	myself = _main_thread;
	myself->self = pthread_self();
	myself->state = ST_RUNNING;
	TAILQ_INSERT_HEAD(&thread_list, myself, entry);

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
		myself->atfork = 1;
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
	TAILQ_FOREACH_SAFE(t, &thread_list, entry, next) {
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

/*
 * thread_list_add:
 *  Add a thread to the thread list. The lock is acquired during
 *  thread_list_add and should not be held by the caller.
 *
 *  Note: Caller of thread_list_add should be the new thread that is
 *  being insterted into the list (called during thread_start trampoline
 *  function which wraps pthread_create start_routine function).
 */
static void
thread_list_add(void)
{
	xnd_assert(myself != NULL);

	thread_list_acquire();
	TAILQ_INSERT_HEAD(&thread_list, myself, entry);
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
	TAILQ_REMOVE(&thread_list, t, entry);
	if (t->joined)
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
	xpthread_mutex_lock(&ckpt_thread.lock);

	TAILQ_FOREACH(t, &thread_list, entry) {
		xpthread_mutex_lock(&t->lock);
	}
	TAILQ_FOREACH(t, &zombie_list, entry) {
		xpthread_mutex_lock(&t->lock);
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
	TAILQ_FOREACH_SAFE(t, &thread_list, entry, next) {
		xpthread_mutex_unlock(&t->lock);
		thread_reap(t);
	}
	TAILQ_FOREACH_SAFE(t, &zombie_list, entry, next) {
		xpthread_mutex_unlock(&t->lock);
		thread_reap(t);
	}

	xpthread_mutex_unlock(&ckpt_thread.lock);
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
	pthread_mutex_lock(&ckpt_thread.lock);
	xnd_assert(myself == _main_thread);
	myself->atfork = 0;
	pthread_cond_signal(&ckpt_thread.cond);
	pthread_mutex_unlock(&ckpt_thread.lock);
}

/*
 * thread_list_atfork_parent:
 *  Release all locks that were acquired in thread_list_atfork_prepare.
 */
void
thread_list_atfork_parent(void)
{
	struct thread_info *t;

	TAILQ_FOREACH(t, &zombie_list, entry) {
		xpthread_mutex_unlock(&t->lock);
	}
	TAILQ_FOREACH(t, &thread_list, entry) {
		xpthread_mutex_unlock(&t->lock);
	}

	xpthread_mutex_unlock(&ckpt_thread.lock);
	xpthread_mutex_unlock(&ckpt_mtx);
        zombie_list_release();
        thread_list_release();
}

void
thread_list_atfork_failed(void)
{
	struct thread_info *t;

	TAILQ_FOREACH(t, &zombie_list, entry) {
		xpthread_mutex_unlock(&t->lock);
	}
	TAILQ_FOREACH(t, &thread_list, entry) {
		xpthread_mutex_unlock(&t->lock);
	}

	xpthread_mutex_unlock(&ckpt_thread.lock);
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
	TAILQ_FOREACH_SAFE(t, &zombie_list, entry, next) {
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
	TAILQ_FOREACH_SAFE(t, &zombie_list, entry, next) {
		if (t->joined)
			zombie_list_remove(t);
	}
	zombie_list_release();
}

static void
zombie_list_add(struct thread_info *t)
{
	if (t->joined) {
		thread_reap(t);
		return;
	}

	zombie_list_acquire();
	TAILQ_INSERT_HEAD(&zombie_list, t, entry);
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
	xnd_assert(t->joined);
	TAILQ_REMOVE(&zombie_list, t, entry);
	thread_reap(t);
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
	void **tsd_keys = NULL;
	struct thread_info *t = NULL;

	t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;

	t->start_routine = start_routine;
	t->arg = arg;
	t->state = ST_EMBRYO;

	err = posix_memalign((void **)&tsd_keys, TSD_ALIGN, TSD_SIZE);
	t->tsd_keys = tsd_keys;
	if (err != 0) {
		free(t);
		return NULL;
	}

	err = pthread_mutex_init(&t->lock, NULL);
	if (err) {
		free(t->tsd_keys);
		free(t);
		return NULL;
	}

	err = pthread_cond_init(&t->cond, NULL);
	if (err) {
		pthread_mutex_destroy(&t->lock);
		free(t->tsd_keys);
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
	xpthread_mutex_destroy(&t->lock);
	xpthread_cond_destroy(&t->cond);
	free(t->tsd_keys);
	free(t);
}

void
thread_exit(void *exit_value)
{
	xpthread_mutex_lock(&myself->lock);
	xnd_tlv_fini();

	/*
	 * Signal a waiter, if any, in __pthread_join_hook. Once,
	 * t->exiting is non-zero, the waiter can call the real
	 * pthread_join while checkpointing is disabled.
	 */
	myself->exiting = 1;
	pthread_cond_signal(&myself->cond);
	xpthread_mutex_unlock(&myself->lock);

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
	bool ready = false;
	void *arg = (void *)&ready;

	bzero(&ckpt_thread, sizeof(ckpt_thread));
	ckpt_thread.state = ST_CKPT_THREAD;

	xpthread_mutex_init(&ckpt_thread.lock, NULL);
	xpthread_cond_init(&ckpt_thread.cond, NULL);

	xpthread_mutex_lock(&ckpt_thread.lock);
	xpthread_create(&ckpt_thread.self, NULL, ckpt_thread_work, arg);
	while (!ready) {
		xpthread_cond_wait(&ckpt_thread.cond, &ckpt_thread.lock);
	}
	xpthread_mutex_unlock(&ckpt_thread.lock);
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
	if (wait_for_ckpt_request_from_coord() != 0) {
		ckpt_thread_exit();
		unreachable();
	}

	set_xnd_state(XND_CKPT_PENDING);
}

static void *
ckpt_thread_work(void *ready)
{
        sigset_t set;
        static volatile bool restart;

	/*
	 * Block checkpoint signal in checkpoint thread. Additionally,
	 * block termination signals so user threads can handle
	 * termination signals.
	 */
        sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGQUIT);
        sigaddset(&set, env_get_ckpt_signal());
        xpthread_sigmask(SIG_BLOCK, &set, NULL);

        xnd_tlv_init();
        myself = &ckpt_thread;
	xnd_assert(myself->self == pthread_self());

        /*
	 * Signal to main thread that initialization is finished
	 * and the checkpoint thread is ready to proceed.
         */
        pthread_mutex_lock(&myself->lock);
	*(bool *)ready = true;
        pthread_cond_signal(&myself->cond);

	/*
	 * If this is a child process handling atfork routines, park
	 * here until the main thread finishes executing atfork
	 * handlers.
	 */
	while (_main_thread->atfork) {
		pthread_cond_wait(&myself->cond, &myself->lock);
	}
        pthread_mutex_unlock(&myself->lock);

        restart = false;
        getcontext(&myself->uctx);

        if (restart) {
                xnd_postrestart_early();

                thread_restore_tls(&ckpt_thread);
		xnd_tlv_init();

                myself = &ckpt_thread;
                myself->self = pthread_self();

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
                thread_save_sig_state();

                /*
                 * Suspend user threads and transition from XND_CKPTPENDING
                 * to XND_SUSPINPROG.
                 */
                suspend_threads();
                wait_for_exiting_threads();
		zombie_list_filter();

                /*
                 * Wait for all threads to arrive at the barrier and
                 * transition from XND_SUSPINPROG -> XND_CKPTINPROG.
                 */
                barrier_arrival_wait();

                xnd_precheckpoint();
		xnd_tlv_fini();
                xnd_checkpoint(&myself->uctx);

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
        xpthread_mutex_destroy(&ckpt_thread.lock);
        xpthread_cond_destroy(&ckpt_thread.cond);
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
		if (sigismember(&proc_siglist, sig))
			kill(pid, sig);
	}
}

void
suspend_threads(void)
{
	bool rescan, hit;
	int err, sig, ckpt_sig, suspended;
	struct thread_info *t, *next;
	enum thread_state ts;

        set_xnd_state(XND_SUSPINPROG);
	ckpt_sig = env_get_ckpt_signal();
        barrier_arrived = 0;

	xpthread_mutex_lock(&ckpt_mtx);
again:
        thread_list_acquire();
        suspended = 0;
        rescan = false;

	TAILQ_FOREACH_SAFE(t, &thread_list, entry, next) {
		xnd_assert(t->state != ST_CKPT_THREAD);
		if (t->exiting)
			continue;
		if (t->joined) {
			thread_list_remove(t);
			continue;
		}

		sig = 0;
		xpthread_mutex_lock(&t->lock);
		ts = atomic_load_explicit(&t->state, memory_order_acquire);
		switch (ts) {
		case ST_RUNNING:
			sig = ckpt_sig;
			hit = thread_state_cas(t, ST_RUNNING, ST_SIGNALED);
			if (!hit) {
				rescan = true;
				break;
			}
			/* fallthrough */
		case ST_SIGNALED:
			err = pthread_kill(t->self, sig);
			if (err == ESRCH) {
				xpthread_mutex_unlock(&t->lock);
				thread_list_remove(t);
				continue;
			}
			/* fallthrough */
		case ST_UNSAFE:
		case ST_EMBRYO:
			rescan = true;
			break;
		case ST_SUSPENDED:
		case ST_SUSPINPROG:
			suspended++;
			break;
		default:
			unreachable();
		}
		xpthread_mutex_unlock(&t->lock);
        }

        thread_list_release();
        if (rescan) {
                usleep(50);
                goto again;
        }

        barrier_expected = suspended;
	xpthread_mutex_unlock(&ckpt_mtx);
}

void
restore_threads(void)
{
	sigset_t list, mask;
	struct thread_info *t;

	xpthread_mutex_lock(&ckpt_mtx);
	thread_list_acquire();

	barrier_arrived = 0;
	barrier_expected = 0;

	sigfillset(&mask);
	sigemptyset(&list);

	TAILQ_FOREACH(t, &thread_list, entry) {
		sigandset(&list, &mask, &t->siglist);
		mask = list;
		barrier_expected++;
		xpthread_create(&t->self, NULL, thread_restart, t);
	}

	proc_siglist = list;
	thread_list_release();
	xpthread_mutex_unlock(&ckpt_mtx);
}

void
wait_for_exiting_threads(void)
{
	int err, exiting, killed;
	struct thread_info *t, *next;

	do {
		killed = 0;
		exiting = 0;
		thread_list_acquire();

		TAILQ_FOREACH_SAFE(t, &thread_list, entry, next) {
			if (t->joined) {
				thread_list_remove(t);
			} else if (t->exiting) {
				exiting++;
				err = pthread_kill(t->self, 0);
				if (err == ESRCH) {
					thread_list_remove(t);
					killed++;
				}
			}
		}

		thread_list_release();
		if (exiting != killed)
			usleep(50);
	} while (exiting != killed);
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

void thread_sighandler(int sig, siginfo_t *info, void *uctx)
{
        static _Thread_local volatile bool is_restart;

        xnd_assert(myself != NULL);
        if (unlikely(myself->state == ST_CKPT_THREAD)) {
                return;
        }
        xnd_assert(thread_state_cas(myself, ST_SIGNALED, ST_SUSPINPROG));

        /* Save state and transition to suspended */
        thread_save_tls();
        thread_save_sig_state();

        is_restart = false;
        getcontext(&myself->uctx);
	if (is_restart)
		return;

        is_restart = true;
	xnd_tlv_fini();
        xnd_assert(thread_state_cas(myself, ST_SUSPINPROG, ST_SUSPENDED));

        /* Wait in barrier before resuming */
        thread_barrier();
	xnd_tlv_init();
        xnd_assert(thread_state_cas(myself, ST_SUSPENDED, ST_RUNNING));
}

void *
thread_start(void *thread)
{
	void *exit_value;

	xnd_tlv_init();

	myself = (struct thread_info *)thread;
	myself->self = pthread_self();
	thread_list_add();

	/*
	 * The parent thread in __pthread_create_hook is waiting
	 * for this thread to be added to the thread list and
	 * ready to execute; signal the parent now.
	 */
	pthread_mutex_lock(&myself->lock);
	myself->state = ST_RUNNING;
	pthread_cond_signal(&myself->cond);
	pthread_mutex_unlock(&myself->lock);

	/*
	 * We only return from this if the thread does not manually
	 * exit via pthread_exit, but a wrapper around pthread_exit
	 * will ensure that the thread still goes through thread_exit
	 * regardless.
	 */
	exit_value = (*myself->start_routine)(myself->arg);
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
	myself->state = ST_RUNNING;
	xnd_assert(myself->self == pthread_self());

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
	if (myself != &ckpt_thread) {
		xnd_assert(malloc_size(myself->tsd_keys) >= TSD_SIZE);
		myself->tls = tls;
		xnd_tsd_copy(myself->tsd_keys, (void **)tls);
		return;
	}

	/*
	 * [ Checkpoint thread falls through here ]
	 *  If this is the first checkpoint, the checkpoint thread
	 *  should save tls register as is. On restart, the checkpoint
	 *  thread will have a new TCB and can copy from saved
	 *  tls to the new TCB's tls.
	 *
	 *  Otherwise, if the checkpoint thread is not saving tls
	 *  for the first time, copy from current TCB to the original
	 *  TCB. Because the original TCB is preserved from vm
	 *  checkpoint/restore, it can be recycled as a buffer.
	 *  Thus, during restore, the checkpoint thread can always
	 *  copy from its original tls to current/new tls.
	 */
	if (myself->tls == 0) {
		myself->tls = tls;
		return;
	}
	xnd_tsd_copy((void *)myself->tls, (void **)tls);
}

static void
thread_restore_tls(struct thread_info *t)
{
	void **src;
	uintptr_t tls;

	xnd_assert(t != NULL);
	asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
	// set_thread_cleanup_stack(tls, NULL);

	/*
	 * Checkpoint thread copies thread-local storage from original
	 * TCB to new TCB. User threads copy thread-local storage from
	 * allocated buffer to current TCB.
	 */
	src = (t == &ckpt_thread ? (void **)t->tls : t->tsd_keys);
	xnd_tsd_copy((void **)tls, src);
}

static void
thread_restore_context(void)
{
	u64 *fp = (u64 *)get_ucontext_fp(&myself->uctx);

	/*
	 * Re-sign every link register saved in a stack frame before
	 * we restore the saved user context. Then, we can jump back
	 * to the previous user context safely.
	 */
	ptrauth_resign_frames(fp);
	xnd_setcontext(&myself->uctx);

        unreachable();
}

static void
thread_save_sig_state(void)
{
	int err, ret, ckpt_sig = env_get_ckpt_signal();

	err = pthread_sigmask(SIG_SETMASK, NULL, &myself->sigmask);
	if (err != 0) {
		xnd_warn("pthread_sigmask: %s\n", strerror(err));
		sigemptyset(&myself->sigmask);
	}

	if (myself->state == ST_CKPT_THREAD &&
	    !sigismember(&myself->sigmask, ckpt_sig)) {
		xnd_warn("ckpt signal isn't blocked by ckpt thread\n");
		sigaddset(&myself->sigmask, ckpt_sig);
	}

	ret = sigpending(&myself->siglist);
	if (ret != 0) {
		xnd_perror("sigpending");
		sigemptyset(&myself->siglist);
	}

	ret = sigaltstack(NULL, &myself->ss);
	if (ret != 0) {
		xnd_perror("sigaltstack");
		myself->ss.ss_sp = NULL;
		myself->ss.ss_size = 0;
		myself->ss.ss_flags = SS_DISABLE;
	}
}

/*
 * thread_restore_sig_state:
 *  Restore signal mask and alternate signal stack if necessary.
 *  Checks that the checkpoint thread is blocking the signal being
 *  used for checkpoints.
 */
static void
thread_restore_sig_state(void)
{
	int err, ret, ckpt_sig = env_get_ckpt_signal();

	if (myself->state == ST_CKPT_THREAD &&
	    !sigismember(&myself->sigmask, ckpt_sig)) {
		xnd_warn("ckpt thread isn't blocking ckpt signal\n");
		sigaddset(&myself->sigmask, ckpt_sig);
	}

	err = pthread_sigmask(SIG_SETMASK, &myself->sigmask, NULL);
	if (err != 0)
		xnd_warn("pthread_sigmask: %s\n", strerror(err));

	if (myself->ss.ss_sp == NULL || (myself->ss.ss_flags & SS_DISABLE))
		return;

	myself->ss.ss_flags &= ~SS_ONSTACK;
	ret = sigaltstack(&myself->ss, NULL);
	if (ret != 0)
		xnd_warn("sigaltstack: %s\n", strerror(errno));

	/* Re-raise non-global pending signals */
	for (int sig = 1; sig < NSIG; sig++) {
		if (sigismember(&proc_siglist, sig))
			continue;
		if (sigismember(&myself->siglist, sig) &&
		    sigismember(&myself->sigmask, sig))
			pthread_kill(myself->self, sig);
	}
}
