/* thread_info.c */
#include "xnd/xnd.h"
#include "xnd/thread_info.h"
#include "xnd/xnd_lib.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/wrappers/signal_wrappers.h"
#include "xnd/wrappers/pthread_wrappers.h"
#include "xnd/platform/ucontext/ucontext.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

_Thread_local struct thread_info        *myself         = NULL;
static struct thread_info               *_main_thread   = NULL;
static struct thread_info               ckpt_thread;

static struct thread_list               thread_list;
static struct thread_list               zombie_list;

static int              threads_expected;
static int              threads_arrived;
static u64              barrier_epoch   = 0ull;
static pthread_cond_t   cond_arrived    = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   cond_released   = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t  ckpt_mtx        = PTHREAD_MUTEX_INITIALIZER;

void thread_list_init(void)
{
        pthread_mutexattr_t attr;

        pthread_mutexattr_init(&attr);
#if DEVELOPMENT || DEBUG
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
        pthread_mutex_init(&thread_list.lock, &attr);

        /* Initialize main thread info */
        tlv_init();
        thread_list.head = calloc(1, sizeof(struct thread_info));
        xnd_assert(thread_list.head != NULL);

        myself = thread_list.head;
        myself->self = pthread_self();
        myself->state = ST_RUNNING;
        _main_thread = myself;

        pthread_mutex_init(&myself->lock, &attr);
        pthread_cond_init(&myself->cond, NULL);
        
        zombie_list_init();
        ckpt_thread_init();
        pthread_mutexattr_destroy(&attr);
}

void thread_list_destroy(void)
{
        struct thread_info *th, *next;
        
        ckpt_thread_reap();
        
        thread_list_acquire();
        for (th = thread_list.head; th; th = next) {
                next = th->next;
                pthread_mutex_destroy(&th->lock);
                pthread_cond_destroy(&th->cond);
                free(th);
        }
        thread_list_release();
        pthread_mutex_destroy(&thread_list.lock);

        zombie_list_destroy();
        tlv_exit();
}

void thread_list_acquire(void)
{
        xnd_assert(pthread_mutex_lock(&thread_list.lock) == 0);
}

void thread_list_release(void)
{
        xnd_assert(pthread_mutex_unlock(&thread_list.lock) == 0);
}

/**
 * thread_list_add:
 *  Add a thread to the thread list. The lock is acquired during
 *  thread_list_add and should not be held by the caller.
 *
 *  Note: Caller of thread_list_add should be the new thread that is
 *  being insterted into the list (called during thread_start trampoline
 *  function which wraps pthread_create start_routine function).
 */
void thread_list_add(void)
{
        thread_list_acquire();
        xnd_assert(myself != NULL);

        myself->next = thread_list.head;
        myself->prev = NULL;

        if (myself->next)
                myself->next->prev = myself;
        
        thread_list.head = myself;
        thread_list_release();
}

/**
 * thread_list_remove:
 *  Remove a thread from the thread list. Only the checkpoint thread
 *  should ever call thread_list_remove; additionally, the checkpoint
 *  thread should be have the lock acquired before calling
 *  thread_list_remove.
 */
void thread_list_remove(struct thread_info *th)
{
        if (th->prev) {
                th->prev->next = th->next;
        } else {
                xnd_assert(th == thread_list.head);
                thread_list.head = th->next;
        }

        if (th->next)
                th->next->prev = th->prev;
        
        /**
         * If the thread is joined, free all resources. Otherwise, a user
         * thread will eventually call pthread_join and start executing
         * in __pthread_join_hook. This user thread will need the thread
         * descriptor to be around in order to call pthread_join with the
         * correct pthread_t. In this case, add the thread to a zombie
         * list to keep it around as long another user thread might need
         * the thread descriptor.
         */
        if (th->joined)
                thread_reap(th);
        else
                zombie_list_add(th);
}

void zombie_list_init(void)
{
        zombie_list.head = NULL;
#if DEVELOPMENT || DEBUG
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        pthread_mutex_init(&zombie_list.lock, &attr);
#else
        pthread_mutex_init(&zombie_list.lock, NULL);
#endif
}

void zombie_list_destroy(void)
{
        struct thread_info *th, *next;
        
        zombie_list_acquire();
        for (th = zombie_list.head; th; th = next) {
                next = th->next;
                thread_reap(th);
        }
        zombie_list_release();
        pthread_mutex_destroy(&zombie_list.lock);
}

void zombie_list_acquire(void)
{
        xnd_assert(pthread_mutex_lock(&zombie_list.lock) == 0);
}

void zombie_list_release(void)
{
        xnd_assert(pthread_mutex_unlock(&zombie_list.lock) == 0);
}

void zombie_list_filter(void)
{
        struct thread_info *th, *next;
        
        zombie_list_acquire();
        for (th = zombie_list.head; th; th = next) {
                next = th->next;
                if (th->joined)
                        zombie_list_remove(th);
        }
        zombie_list_release();
}

void zombie_list_add(struct thread_info *th)
{
        if (unlikely(th->joined)) {
                thread_reap(th);
                return;
        }
        
        zombie_list_acquire();
        th->next = zombie_list.head;
        th->prev = NULL;

        if (th->next)
                th->next->prev = th;
        
        zombie_list.head = th;
        zombie_list_release();
}

/**
 * zombie_list_remove:
 *  If a exited thread's thread descriptor isn't needed anymore, the
 *  zombie thread can be removed the zombie list and its resources can
 *  be freed. zombie_list_remove should only be called directly by
 *  zombie_list_filter (to scan for thread desciptors that are no longer
 *  needed), and the lock should be held by the caller.
 */
void zombie_list_remove(struct thread_info *zombie)
{
        xnd_assert(zombie->joined);
        if (zombie->prev) {
                zombie->prev->next = zombie->next;
        } else {
                xnd_assert(zombie == zombie_list.head);
                zombie_list.head = zombie->next;
        }

        if (zombie->next)
                zombie->next->prev = zombie->prev;

        thread_reap(zombie);
}

/**
 * thread_init:
 *  Initialize new thread with start routine and argument that were
 *  included as arguments to pthread_create.
 *
 *  thread_init should be called by the pthread_create wrapper to
 *  initialize a new thread, and the thread should added to the thread
 *  list by calling thread_list_add in the thread start routine
 *  wrapper/trampoline function.
 */
struct thread_info *thread_init(void *(*fn)(void *), void *arg)
{
        struct thread_info *    new;
        pthread_mutexattr_t     attr;

        new = calloc(1, sizeof(struct thread_info));
        xnd_assert(new != NULL);
        
        new->fn = fn;
        new->arg = arg;
        new->state = ST_EMBRYO;

        pthread_mutexattr_init(&attr);
#if DEVELOPMENT || DEBUG
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#endif
        xnd_assert(pthread_mutex_init(&new->lock, &attr) == 0);
        xnd_assert(pthread_cond_init(&new->cond, NULL) == 0);

        pthread_mutexattr_destroy(&attr);
        return new;
}

/**
 * thread_reap:
 *  Free all resources associated with this thread. thread_reap should
 *  be called once a thread has exited and been joined by another thread.
 */
void thread_reap(struct thread_info *zombie)
{
        xnd_assert(zombie->joined);
        pthread_mutex_destroy(&zombie->lock);
        pthread_cond_destroy(&zombie->cond);
        free(zombie);
}

__noreturn void thread_exit(void *exit_value)
{
        xnd_assert(myself);
        xnd_assert(pthread_mutex_lock(&myself->lock) == 0);
        
        tlv_exit();
        myself->exiting = 1;

        pthread_cond_signal(&myself->cond);
        xnd_assert(pthread_mutex_unlock(&myself->lock) == 0);
        
        pthread_exit(exit_value);
        unreachable();
}

struct thread_info *thread_self(void)
{
        assert(myself != NULL);
        return myself;
}

struct thread_info *thread_self_or_null(void)
{
        return myself;
}

struct thread_info *main_thread(void)
{
        assert(_main_thread != NULL);
        return _main_thread;
}

void ckpt_thread_init(void)
{
        bool ready = false;

        ckpt_thread.state = ST_CKPT_THREAD;
        pthread_mutex_init(&ckpt_thread.lock, NULL);
        pthread_cond_init(&ckpt_thread.cond, NULL);

        pthread_mutex_lock(&ckpt_thread.lock);
        pthread_create(&ckpt_thread.self, NULL, ckpt_thread_work,
                       (void *)&ready);
        while (!ready) {
                pthread_cond_wait(&ckpt_thread.cond, &ckpt_thread.lock);
        }
        pthread_mutex_unlock(&ckpt_thread.lock);
}

__noreturn void ckpt_thread_exit(void)
{
        xnd_assert(myself == &ckpt_thread);
        xnd_assert(pthread_mutex_lock(&myself->lock) == 0);
        
        tlv_exit();
        myself->exiting = 1;
        myself->exit_value = NULL;
        
        pthread_cond_signal(&myself->cond);
        xnd_assert(pthread_mutex_unlock(&myself->lock) == 0);
        
        pthread_exit(NULL);
        unreachable();
}

void ckpt_thread_wait(void)
{
        sigset_t        set;
        int             err, sig = 0;

        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);

        while (sig != SIGUSR2) {
                err = sigwait(&set, &sig);
                if (get_xnd_state() == XND_EXITING)
                        ckpt_thread_exit();
                else if (unlikely(err != 0))
                        xnd_warn("sigwait: %s\n", strerror(err));
        }
}

void *ckpt_thread_work(void *ready)
{
        static volatile bool restart;
        {
                sigset_t set;

                sigemptyset(&set);
                sigaddset(&set, SIGUSR1);
                sigaddset(&set, SIGUSR2);
                pthread_sigmask(SIG_BLOCK, &set, NULL);
        }
       
        tlv_init();
        myself = &ckpt_thread;
        myself->self= pthread_self();
        
        /**
         * Signal to main thread that the checkpoint thread has
         * initialized itself and is ready
         */
        pthread_mutex_lock(&myself->lock);
        *(bool *)ready = true;
        pthread_cond_signal(&myself->cond);
        pthread_mutex_unlock(&myself->lock);

        restart = false;
        getcontext(&myself->uctx);

        if (restart) {
                xnd_postrestart();

                tlv_init();
                myself = &ckpt_thread;
                myself->self = pthread_self();

                restore_threads();
                barrier_arrival_wait();
                thread_restore_tls();
                thread_restore_sig_state();
                sig_state_restore();
                zombie_list_filter();

                set_xnd_state(XND_RUNNING);
                barrier_release();
        }
        
        restart = true;
        for (;;) {
                ckpt_thread_wait();
                
                thread_save_tls();
                thread_save_sig_state();

                set_xnd_state(XND_CKPTINPROG);
                suspend_threads();
                barrier_arrival_wait();
                wait_for_exiting_threads();

                xnd_precheckpoint();
                set_tls_slot(TLS_TLV_FLAG_SLOT, 0);
                xnd_checkpoint(&myself->uctx);
                
                set_tls_slot(TLS_TLV_FLAG_SLOT, TLS_TLV_INIT_MAGIC);
                set_xnd_state(XND_RUNNING);
                barrier_release();
        }

        return NULL;
}

void ckpt_thread_reap(void)
{
        if (pthread_kill(ckpt_thread.self, SIGUSR2) != 0)
                ckpt_thread_terminate();
        else
                ckpt_thread_join();

        pthread_mutex_destroy(&ckpt_thread.lock);
        pthread_cond_destroy(&ckpt_thread.cond);
}

void ckpt_thread_join(void)
{
        int err;

        pthread_mutex_lock(&ckpt_thread.lock);
        while (!ckpt_thread.exiting) {
                pthread_cond_wait(&ckpt_thread.cond, &ckpt_thread.lock);
        }
        pthread_mutex_unlock(&ckpt_thread.lock);

        if ((err = pthread_join(ckpt_thread.self, NULL)) != 0)
                xnd_error("pthread_join: %s\n", strerror(err));
}

void ckpt_thread_terminate(void)
{
        uintptr_t       tls;
        mach_port_t     port;
        kern_return_t   kr;

        tls = (uintptr_t)ckpt_thread.self + PTHREAD_T_TLS_OFFSET;
        port = (mach_port_t)(uintptr_t)((void **)tls)[__TSD_MACH_THREAD_SELF];
        if ((kr = thread_terminate(port)) != KERN_SUCCESS)
                xnd_error("thread_terminate: %s\n", mach_error_string(kr));
}

void barrier_arrival_wait(void)
{
        pthread_mutex_lock(&ckpt_mtx);
        while (threads_arrived < threads_expected) {
                pthread_cond_wait(&cond_arrived, &ckpt_mtx);
        }
        pthread_mutex_unlock(&ckpt_mtx);
}

void barrier_release(void)
{
        pthread_mutex_lock(&ckpt_mtx);
        barrier_epoch++;
        pthread_cond_broadcast(&cond_released);
        pthread_mutex_unlock(&ckpt_mtx);
}

void suspend_threads(void)
{
        struct thread_info      *th, *next;
        int                     err, suspended;
        bool                    rescan;

        pthread_mutex_lock(&ckpt_mtx);
        thread_list_acquire();

        threads_arrived = 0;
again:
        suspended = 0;
        rescan = false;
        for (th = thread_list.head; th; th = next) {
                next = th->next;
                xnd_assert(th->state != ST_CKPT_THREAD);
                if (th->exiting) {
                        continue;
                } else if (th->joined) {
                        thread_list_remove(th);
                        continue;
                }

                if (th->state == ST_RUNNING &&
                    thread_state_cas(th, ST_RUNNING, ST_SIGNALED)) {
                        err = pthread_kill(th->self, SIGUSR1);
                        if (err == ESRCH) {
                                thread_list_remove(th);
                                continue;
                        } else if (unlikely(err != 0)) {
                                xnd_warn("pthread_kill: %s\n", strerror(err));
                        }
                        rescan = true;
                } else if (th->state == ST_SIGNALED) {
                        err = pthread_kill(th->self, 0);
                        if (err == ESRCH) {
                                thread_list_remove(th);
                                continue;
                        } else if (unlikely(err != 0)) {
                                xnd_warn("pthread_kill: %s\n", strerror(err));
                        }
                        rescan = true;
                } else if (th->state == ST_SUSPENDED ||
                           th->state == ST_SUSPINPROG) {
                        suspended++;
                } else if (th->state == ST_UNSAFE) {
                        rescan = true;
                }
        }

        if (rescan) {
                usleep(10);
                goto again;
        }
        
        threads_expected = suspended;
        thread_list_release();
        pthread_mutex_unlock(&ckpt_mtx);
}

void restore_threads(void)
{
        int                     err;
        struct thread_info      *th;

        thread_list_acquire();
        pthread_mutex_lock(&ckpt_mtx);
        
        threads_arrived = 0;
        threads_expected = 0;

        for (th = thread_list.head; th; th = th->next) {
                xnd_assert(!th->exiting);
                err = pthread_create(&th->self, NULL, thread_restart, th);
                if (unlikely(err != 0)) {
                        xnd_error("pthread_create: %s\n", strerror(err));
                        xnd_abort();
                }
                threads_expected++;
        }

        thread_list_release();
        pthread_mutex_unlock(&ckpt_mtx);
}

void wait_for_exiting_threads(void)
{
        struct thread_info      *th, *next;
        int                     exiting, killed;
        
        thread_list_acquire();
again:
        killed = 0;
        exiting = 0;
        for (th = thread_list.head; th; th = next) {
                next = th->next;
                if (th->joined) {
                        thread_list_remove(th);
                } else if (th->exiting) {
                        exiting++;
                        if (pthread_kill(th->self, 0) == ESRCH) {
                                thread_list_remove(th);
                                killed++;
                        }
                } else {
                        xnd_assert(th->state == ST_SUSPENDED ||
                                   th->state == ST_SUSPINPROG);
                }
        }

        if (exiting != killed) {
                usleep(10);
                goto again;
        }
        thread_list_release();
}

void thread_barrier(void)
{
        int local_epoch;

        xnd_assert(pthread_mutex_lock(&ckpt_mtx) == 0);
        local_epoch = barrier_epoch;
        
        if (++threads_arrived == threads_expected)
                pthread_cond_signal(&cond_arrived);

        while (local_epoch == barrier_epoch) {
                pthread_cond_wait(&cond_released, &ckpt_mtx);
        }

        xnd_assert(pthread_mutex_unlock(&ckpt_mtx) == 0);
}

void thread_sighandler(int sig, siginfo_t *info, void *uctx)
{
        xnd_assert(myself != NULL);
        if (unlikely(myself->state == ST_CKPT_THREAD)) {
                return;
        }
        xnd_assert(thread_state_cas(myself, ST_SIGNALED, ST_SUSPINPROG));
        
        /* Save state and transition to suspended */
        thread_save_tls();
        thread_save_sig_state();
        thread_save_context((ucontext_t *)uctx);
        set_tls_slot(TLS_TLV_FLAG_SLOT, 0);
        xnd_assert(thread_state_cas(myself, ST_SUSPINPROG, ST_SUSPENDED));
        
        /* Wait in barrier and then resume */
        thread_barrier();
        set_tls_slot(TLS_TLV_FLAG_SLOT, TLS_TLV_INIT_MAGIC);
        xnd_assert(thread_state_cas(myself, ST_SUSPENDED, ST_RUNNING));
}

__noreturn void *thread_start(void *thread)
{
        void *retval;
        
        tlv_init();
        /**
         * Set thread local pointer to thread descriptor to point to
         * newly allocated thread struct, and initialize pthread_t
         * field.
         */
        myself = (struct thread_info *)thread;
        myself->self = pthread_self();
        thread_list_add();
        
        /**
         * Signal to thread that spawned this thread in 
         * __pthread_create_hook that this thread has started and added 
         * itself to the thread list.
         */
        pthread_mutex_lock(&myself->lock);
        myself->state = ST_RUNNING;
        pthread_cond_signal(&myself->cond);
        pthread_mutex_unlock(&myself->lock);

        retval = myself->fn(myself->arg);
        thread_exit(retval);

        unreachable();
}

__noreturn void *thread_restart(void *thread)
{
        tlv_init();
        
        /**
         * Reinitialize thread local struct thread_info pointer to
         * the current thread
         */
        myself = (struct thread_info *)thread;
        myself->self = pthread_self();
        myself->state = ST_RUNNING;
        
        /* Restore tls/tsd and signal state */
        thread_restore_tls();
        thread_restore_sig_state();
        
        /**
         * Wait in barrier until all threads have restored their
         * tls and signal state. Then, restore context via setcontext().
         */
        thread_barrier();
        thread_restore_context();

        unreachable();
}

void thread_save_tls(void)
{
        assert(myself != NULL);
        asm volatile("mrs %0, tpidrro_el0" : "=r" (myself->tls) :: "memory");
}

/**
 * thread_restore_tls:
 *  TPIDRRO_EL0 can not be written to directly. Instead, restore tsd 
 *  slots 125-209 and 256-767 for thread locals.
 *  Also restore per-thread cleanup stack that was located in the
 *  thread's previous struct pthread_s.
 *
 * From Apple's libpthread:
 *      Keys 0 - 9 are for Libsyscall/libplatform usage
 *      Keys 10 - 29 are for Libc/Libsystem internal usage
 *      Keys 20-29,120-125 for libdispatch usage
 *      Keys 30-255 for Non Libsystem usage
 *      Keys 30-39 for Graphic frameworks usage
 *      Keys 40-49 for Objective-C runtime usage
 *      Keys 50-59 for Core Foundation usage
 *      Keys 60-69 for Foundation usage
 *      Keys 70-79 for Core Animation/QuartzCore usage
 *      Keys 80-89 for CoreData
 *      Keys 90-94 for JavaScriptCore Collection
 *      Keys 95 for CoreText
 *      Keys 100-109 are for the Swift runtime
 *      Keys 110-115 for libmalloc
 *      Keys 115-120 for libdispatch workgroups
 *      125 - 209 for shared cache dylibs __thread support
 */
void thread_restore_tls(void)
{
        uintptr_t       tls;
        void            **dst, **src;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory");
        set_thread_cleanup_stack(NULL);

        dst = (void **)tls;
        src = (void **)myself->tls;
        for (uint slot = 30; slot < 768; slot++) {
                if (dst[slot] == NULL && src[slot] != NULL)
                        dst[slot] = src[slot];
                else if (dst[slot] != NULL && src[slot] == NULL)
                        dst[slot] = NULL;
        }
}

void thread_save_context(ucontext_t *ucp)
{
        memcpy(&myself->uctx, ucp, sizeof(ucontext_t));
        memcpy(&myself->uctx.__mcontext_data, ucp->uc_mcontext,
               sizeof(myself->uctx.__mcontext_data));

        myself->uctx.uc_mcontext = &myself->uctx.__mcontext_data;
}

__noreturn void thread_restore_context(void)
{
        u64 fp;
        
        fp = get_ucontext_fp(&myself->uctx);
        if (PTRAUTH_SIGNED(fp)) {
                XPACD(fp);
        }

        pac_resign_frames((u64 *)fp);
        xnd_setcontext(&myself->uctx);
        
        unreachable();
}

void thread_save_sig_state(void)
{
        int err;

        err = pthread_sigmask(SIG_SETMASK, NULL, &myself->sigblocked);
        if (err != 0)
                xnd_warn("pthread_sigmask: %s\n", strerror(err));

        if (sigaltstack(NULL, &myself->ss) < 0)
                xnd_warn("sigaltstack: %s\n", strerror(errno));
}

void thread_restore_sig_state(void)
{
        int err;

        err = pthread_sigmask(SIG_SETMASK, &myself->sigblocked, NULL);
        if (err != 0)
                xnd_warn("pthread_sigmask: %s\n", strerror(err));

        if (myself->ss.ss_sp == NULL || (myself->ss.ss_flags & SS_DISABLE))
                return;
        
        err = sigaltstack(&myself->ss, NULL);
        if (err < 0)
                xnd_warn("sigaltstack: %s\n", strerror(errno));
}
