/* thread_info.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <errno.h>
#include <unistd.h>
#include "ckpt.h"
#include "pac.h"
#include "thread_info.h"
#include "tls.h"
#include "pthread_wrappers.h"

_Thread_local struct thread_info        *myself         = NULL;
static struct thread_info               *_main_thread   = NULL;
static struct thread_info               ckpt_thread;
static struct thread_list               thread_list;

static int              threads_expected;
static int              threads_arrived;
static ullong           barrier_epoch   = 0u;
static pthread_cond_t   cond_arrived    = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   cond_released   = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t  ckpt_mtx        = PTHREAD_MUTEX_INITIALIZER;

void thread_list_init(void)
{
        uintptr_t ckpt_thread_ready = 0;
        
        pthread_mutex_init(&thread_list.lock, NULL);

        /* Initialize main thread info */
        thread_list.head = malloc(sizeof(struct thread_info));
        assert(thread_list.head != NULL);
        
        tlv_init();
        myself = thread_list.head;
        myself->self = pthread_self();
        myself->state = ST_RUNNING;
        myself->next = NULL;
        myself->prev = NULL;
        myself->exiting = false;
        _main_thread = myself;

        pthread_mutex_init(&myself->lock, NULL);
        pthread_cond_init(&myself->cond, NULL);
        
        /**
         * Initialize and create the checkpoint thread, then
         * wait for the checkpoint thread to start up before returning
         */
        ckpt_thread.state = ST_CKPT_THREAD;
        pthread_mutex_init(&ckpt_thread.lock, NULL);
        pthread_cond_init(&ckpt_thread.cond, NULL);

        pthread_mutex_lock(&ckpt_thread.lock);
        pthread_create(&ckpt_thread.self, NULL, ckpt_thread_work,
                       (void *)&ckpt_thread_ready);

        while (!ckpt_thread_ready) {
                pthread_cond_wait(&ckpt_thread.cond, &ckpt_thread.lock);
        }

        pthread_mutex_unlock(&ckpt_thread.lock);
}

void thread_list_destroy(void)
{
        struct thread_info *th, *next;

        /* Get checkpoint thread to exit */
        pthread_mutex_lock(&ckpt_mtx);
        pthread_mutex_lock(&ckpt_thread.lock);
        assert(pthread_kill(ckpt_thread.self, SIGTERM) == 0);

        while (!ckpt_thread.exiting) {
                pthread_cond_wait(&ckpt_thread.cond, &ckpt_thread.lock);
        }
        
        assert(pthread_join(ckpt_thread.self, NULL) == 0);
        pthread_mutex_unlock(&ckpt_mtx);
        pthread_mutex_unlock(&ckpt_thread.lock);
        
        pthread_mutex_destroy(&ckpt_thread.lock);
        pthread_cond_destroy(&ckpt_thread.cond);
        
        for (th = thread_list.head; th; th = next) {
                next = th->next;
                thread_reap(th);
        }

        tlv_exit();
}

void thread_list_acquire(void)
{
        pthread_mutex_lock(&thread_list.lock);
}

void thread_list_release(void)
{
        pthread_mutex_unlock(&thread_list.lock);
}

struct thread_info *thread_list_find(pthread_t p)
{
        struct thread_info *th, *ret = NULL;
        
        thread_list_acquire();
        for (th = thread_list.head; th; th = th->next) {
                if (pthread_equal(th->self, p)) {
                        ret = th;
                        break;
                }
        }
        
        thread_list_release();
        return ret;
}

/**
 * thread_list_add:
 *  Add a thread from the thread list and opportunistically remove dead
 *  threads from the list in order to free their resources. The lock is 
 *  acquired during thread_list_add and should not be held by the caller.
 *
 *  Note: Caller of thread_list_add should be the new thread that is
 *  being insterted into the list (called during thread_start trampoline
 *  function which wraps pthread_create start_routine function).
 */
void thread_list_add(void)
{
        struct thread_info *th, *next;
        
        thread_list_acquire();
        /**
         * Find threads that have exited by sending signal 0 and
         * removed zombie threads from the thread list.
         */
        for (th = thread_list.head; th; th = next) {
                next = th->next;
                if (th->exiting && pthread_kill(th->self, 0) == ESRCH)
                        thread_reap(th);
        }
        
        /**
         * Add new thread to the thread list. The caller of
         * thread_list_add should be the new thread itself.
         */
        assert(myself != NULL);
        myself->next = thread_list.head;
        myself->prev = NULL;

        if (myself->next)
                myself->next->prev = myself;
        
        thread_list.head = myself;
        thread_list_release();
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
        struct thread_info *new;

        new = malloc(sizeof(struct thread_info));
        assert(new != NULL);

        new->fn = fn;
        new->arg = arg;
        new->exiting = false;
        new->state = ST_EMBRYO;
        new->next = NULL;
        new->prev = NULL;

        pthread_mutex_init(&new->lock, NULL);
        pthread_cond_init(&new->cond, NULL);

        return new;
}

/**
 * thread_reap:
 *  Remove thread from active thread list and free resources
 *  associated with it. Assume the lock is already held by the
 *  caller.
 */
void thread_reap(struct thread_info *zombie)
{
        if (zombie->prev)
                zombie->prev->next = zombie->next;
        else
                thread_list.head = zombie->next;

        if (zombie->next)
                zombie->next->prev = zombie->prev;
        
        pthread_mutex_destroy(&zombie->lock);
        pthread_cond_destroy(&zombie->cond);

        free(zombie);
}

void thread_exit(void *exit_value)
{
        assert(myself != NULL);
        pthread_mutex_lock(&myself->lock);
        
        tlv_exit();
        myself->exiting = true;
        myself->exit_value = exit_value;

        pthread_cond_signal(&myself->cond);
        pthread_mutex_unlock(&myself->lock);

        pthread_exit(exit_value);
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

void ckpt_thread_exit(void)
{
        assert(myself == &ckpt_thread);
        pthread_mutex_lock(&myself->lock);
        
        tlv_exit();
        myself->exiting = true;
        myself->exit_value = NULL;
        
        pthread_cond_signal(&myself->cond);
        pthread_mutex_unlock(&myself->lock);
        
        pthread_exit(NULL);
}

void ckpt_thread_wait(void)
{
        sigset_t        set;
        int             err, sig = 0;

        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        sigaddset(&set, SIGTERM);

        while (sig != SIGUSR2) {
                err = sigwait(&set, &sig);
                if (unlikely(err != 0)) {
                        fprintf(stderr, "sigwait: %s\n", strerror(err));
                } else if (sig == SIGTERM) {
                        ckpt_thread_exit();
                }
        }
}

void *ckpt_thread_work(void *arg)
{
        static volatile bool restart;
        {
                sigset_t set;

                sigemptyset(&set);
                sigaddset(&set, SIGUSR1);
                sigaddset(&set, SIGUSR2);
                sigaddset(&set, SIGTERM);
                
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
        *(uintptr_t *)arg = 1;
        pthread_cond_signal(&myself->cond);
        pthread_mutex_unlock(&myself->lock);

        restart = false;
        getcontext(&myself->uc);

        if (restart) {
                postrestart();
                restore_threads();

                tlv_init();
                myself = &ckpt_thread;
                myself->self = pthread_self();
                
                barrier_arrival_wait();
                thread_restore_tls();
                thread_restore_sig_state();

                set_ckpt_state(LIBCKPT_RUNNING);
                barrier_release();
        }
        
        restart = true;
        for (;;) {
                ckpt_thread_wait();
                
                thread_save_tls();
                thread_save_sig_state();

                set_ckpt_state(LIBCKPT_CKPTINPROG);
                suspend_threads();
                barrier_arrival_wait();
                wait_for_exiting_threads();

                precheckpoint();
                docheckpoint(&myself->uc);
                
                set_ckpt_state(LIBCKPT_RUNNING);
                barrier_release();
        }

        return NULL;
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

/**
 * scan_threads:
 *  Scan thread list and suspend thread by sending SIGUSR1, and increment
 *  a counter to indentify how many user threads are active and should
 *  be participating in the checkpoint.
 *
 * thread_list.lock should be held by the caller to scan_threads.
 * A positive integer will be returned if any threads are not suspended
 * or confirmed to have exited. Thus, a rescan will be necessary before
 * the suspend_threads phase can complete.
 */
bool scan_threads(u32 *thread_count)
{
        struct thread_info      *th, *next;
        int                     err;
        u32                     count = 0;
        bool                    rescan = false;

        for (th = thread_list.head; th; th = next) {
                next = th->next;

                if (th->exiting) {
                        continue;
                } else if (unlikely(th->state == ST_CKPT_THREAD)) {
                        continue;
                }
        
                if (th->state == ST_RUNNING &&
                    thread_state_cas(th, ST_RUNNING, ST_SIGNALED)) {
                        err = pthread_kill(th->self, SIGUSR1);
                        if (err == ESRCH) {
                                thread_reap(th);
                                continue;
                        } else if (unlikely(err != 0)) {
                                fprintf(stderr, "pthread_kill: %s\n",
                                        strerror(err));
                        }
                        rescan = true;
                } else if (th->state == ST_SIGNALED) {
                        err = pthread_kill(th->self, 0);
                        if (err == ESRCH) {
                                thread_reap(th);
                                continue;
                        } else if (unlikely(err != 0)) {
                                fprintf(stderr, "pthread_kill: %s\n",
                                        strerror(err));
                        }
                        rescan = true;
                } else if (th->state == ST_SUSPENDED ||
                           th->state == ST_SUSPINPROG) {
                        count++;
                } else if (th->state == ST_UNSAFE) {
                        rescan = true;
                }
        }
        
        *thread_count = count;
        return rescan;
}

void suspend_threads(void)
{
        u32 thread_count;
        
        pthread_mutex_lock(&ckpt_mtx);
        threads_arrived = 0;
        
        thread_list_acquire();
        while (scan_threads(&thread_count)) {
                usleep(10);
        }
        thread_list_release();

        threads_expected = thread_count;
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
                assert(!th->exiting);
                err = pthread_create(&th->self, NULL, thread_restart, th);
                if (unlikely(err != 0)) {
                        fprintf(stderr, "pthread_create: %s\n",
                                strerror(err));
                        exit(EXIT_FAILURE);
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
        do {
                killed  = 0;
                exiting = 0;
                
                for (th = thread_list.head; th; th = next) {
                        next = th->next;
                        if (!th->exiting) {
                                continue;
                        }

                        exiting++;
                        if (pthread_kill(th->self, 0) == ESRCH) {
                                killed++;
                                thread_reap(th);
                        }
                }

                if (killed != exiting) {
                        usleep(10);
                }
        } while (killed != exiting);
        thread_list_release();
}

void thread_barrier(void)
{
        int local_epoch;

        pthread_mutex_lock(&ckpt_mtx);
        local_epoch = barrier_epoch;
        threads_arrived++;
        
        if (threads_arrived == threads_expected)
                pthread_cond_signal(&cond_arrived);

        while (local_epoch == barrier_epoch) {
                pthread_cond_wait(&cond_released, &ckpt_mtx);
        }

        pthread_mutex_unlock(&ckpt_mtx);
}

void thread_sighandler(int sig, siginfo_t *info, void *uctx)
{
        assert(myself != NULL);
        if (unlikely(myself->state == ST_CKPT_THREAD)) {
                return;
        }

        if (!thread_state_cas(myself, ST_SIGNALED, ST_SUSPINPROG)) {
                /**
                 * Prevent user threads from entering the signal handler
                 * more than once. A user thread should only be here
                 * if in state ST_SIGNALED (set by the checkpoint thread).
                 */
                return;
        }
        
        /* Save state and transition to suspended */
        thread_save_tls();
        thread_save_sig_state();
        thread_save_context((ucontext_t *)uctx);
        assert(thread_state_cas(myself, ST_SUSPINPROG, ST_SUSPENDED));
        
        /* Wait in barrier and then resume */
        thread_barrier();
        assert(thread_state_cas(myself, ST_SUSPENDED, ST_RUNNING));
}

void *thread_start(void *thread)
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

        return retval;
}

void *thread_restart(void *thread)
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
        asm volatile("mrs %0, tpidrro_el0" : "=r" (myself->tls));
}

/**
 * thread_restore_tls:
 *  TPIDRRO_EL0 can not be written to directly. Instead, restore tsd 
 *  slots 125-209 and 256-767 for thread locals.
 *  Also restore per-thread cleanup stack that was located in the
 *  thread's previous struct pthread_s.
 */
void thread_restore_tls(void)
{
        uintptr_t                               tls;
        void                                    **dst, **src;
        struct __darwin_pthread_handler_rec     *__cleanup;

        asm volatile("mrs %0, tpidrro_el0" : "=r" (tls) :: "memory"); 
        __cleanup = get_thread_cleanup_stack(myself->tls);
        set_thread_cleanup_stack(__cleanup);

        dst = (void **)tls;
        src = (void **)myself->tls;

        for (uint slot = 125; slot < 210; slot++) {
                if (dst[slot] == NULL && src[slot] != NULL)
                        dst[slot] = src[slot];
                else if (dst[slot] != NULL && src[slot] == NULL)
                        dst[slot] = NULL;
        }

        for (uint slot = 256; slot < 768; slot++) {
                if (dst[slot] == NULL && src[slot] != NULL)
                        dst[slot] = src[slot];
                else if (dst[slot] != NULL && src[slot] == NULL)
                        dst[slot] = NULL;
        }
}

void thread_save_context(ucontext_t *ucp)
{
        memcpy(&myself->uc, ucp, sizeof(ucontext_t));
        memcpy(&myself->uc.__mcontext_data, ucp->uc_mcontext,
               sizeof(myself->uc.__mcontext_data));

        myself->uc.uc_mcontext = (mcontext_t)&myself->uc.__mcontext_data;
}

__noreturn void thread_restore_context(void)
{
        u64 fp;
        
        fp = get_ucontext_fp(&myself->uc);
        if (PTRAUTH_SIGNED(fp)) {
                XPACD(fp);
        }

        pac_resign_frames((u64 *)fp);
        pac_patch_context(&myself->uc);
        
        if (setcontext(&myself->uc) < 0) {
                perror("setcontext");
                thread_exit(NULL);
        }
        
        unreachable();
}

void thread_save_sig_state(void)
{
        pthread_sigmask(SIG_SETMASK, NULL, &myself->sigblocked);
}

void thread_restore_sig_state(void)
{
        pthread_sigmask(SIG_SETMASK, &myself->sigblocked, NULL);
}
