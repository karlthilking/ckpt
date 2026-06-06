/* thread_info.h */
#ifndef __CKPT_THREAD_INFO_H__
#define __CKPT_THREAD_INFO_H__
#define _XOPEN_SOURCE
#include <stdatomic.h>
#include <stdbool.h>
#include <ucontext.h>
#include <pthread.h>
#include <signal.h>
#include "types.h"

typedef enum thread_state {
        ST_EMBRYO,
        ST_RUNNING,
        ST_SIGNALED,
        ST_SUSPINPROG,
        ST_SUSPENDED,
        ST_CKPT_THREAD,
        ST_UNSAFE
} thread_state;

struct thread_list {
       struct thread_info       *head;
       pthread_mutex_t          lock;
};

struct thread_info {
        pthread_t               self;
        void                    *(*fn)(void *);
        void                    *arg;

        bool                    exiting;
        void                    *exit_value;
        
        _Atomic thread_state    state;
        ucontext_t              uc;
        uintptr_t               tls;
        sigset_t                sigblocked;

        pthread_mutex_t         lock;
        pthread_cond_t          cond;

        struct thread_info      *next;
        struct thread_info      *prev;
};

static __always_inline bool thread_state_cas(struct thread_info *th,
                                             thread_state expected,
                                             thread_state desired)
{
        return atomic_compare_exchange_strong(&th->state,
                                              &expected, desired);
}

static __always_inline void unsafe_enter(struct thread_info *self)
{
        if (unlikely(self->state == ST_CKPT_THREAD))
                return;
        while (!thread_state_cas(self, ST_RUNNING, ST_UNSAFE));
}

static __always_inline bool unsafe_exit(struct thread_info *self)
{
        if (unlikely(self->state == ST_CKPT_THREAD))
                return true;
        return thread_state_cas(self, ST_UNSAFE, ST_RUNNING);
}

void                    thread_list_init(void);
void                    thread_list_destroy(void);
void                    thread_list_acquire(void);
void                    thread_list_release(void);
struct thread_info      *thread_list_find(pthread_t);
void                    thread_list_add(void);

struct thread_info      *thread_init(void *(*)(void *), void *);
void                    thread_reap(struct thread_info *);
void                    thread_exit(void *);
struct thread_info      *thread_self(void);
struct thread_info      *thread_self_or_null(void);
struct thread_info      *main_thread(void);

void    ckpt_thread_exit(void);
void    ckpt_thread_wait(void);
void    *ckpt_thread_work(void *);

void    barrier_arrival_wait(void);
void    barrier_release(void);

bool    scan_threads(u32 *);
void    suspend_threads(void);
void    restore_threads(void);
void    wait_for_exiting_threads(void);

void    thread_barrier(void);
void    thread_sighandler(int, siginfo_t *, void *);
void    *thread_start(void *);
void    *thread_restart(void *);

void    thread_save_tls(void);
void    thread_restore_tls(void);
void    thread_save_context(ucontext_t *);
void    thread_restore_context(void);
void    thread_save_sig_state(void);
void    thread_restore_sig_state(void);

#endif // __CKPT_THREAD_INFO_H__
