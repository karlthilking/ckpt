/* pthread_wrappers.h */
#ifndef __PTHREAD_WRAPPERS_H__
#define __PTHREAD_WRAPPERS_H__
#include <pthread.h>
#include "inject.h"

#define PTHREAD_SELF_DISCRIMINATOR      0x5B9ULL
#define PTHREAD_MAGIC                   0x7770000000000000ULL
#define PTHREAD_TAG_MASK                0xFFF0000000000000ULL

extern void             *pthread_get_stackaddr_np(pthread_t);
extern size_t           pthread_get_stacksize_np(pthread_t);
extern pthread_t        pthread_main_thread_np(void);
extern int              pthread_main_np(void);

void    __pthread_cookie(void);
void    __pthread_slot_fixup(void);

int             __pthread_create_hook(pthread_t *, const pthread_attr_t *, 
                                      void *(*)(void *), void *);
int             __pthread_join_hook(pthread_t, void **);
void            __pthread_exit_hook(void *);
pthread_t       __pthread_self_hook(void);
int             __pthread_equal_hook(pthread_t, pthread_t);
int             __pthread_kill_hook(pthread_t, int);
int             __pthread_detach_hook(pthread_t);
int             __pthread_sigmask_hook(int, const sigset_t *, sigset_t *);
int             __pthread_setschedparam_hook(pthread_t, int, 
                                             const struct sched_param *);
int             __pthread_getschedparam_hook(pthread_t, int *, 
                                             struct sched_param *);
void            *__pthread_get_stackaddr_np_hook(pthread_t);
size_t          __pthread_get_stacksize_np_hook(pthread_t);
int             __pthread_cancel_hook(pthread_t);
pthread_t       __pthread_main_thread_np_hook(void);
int             __pthread_main_np_hook(void);

INTERPOSE(__pthread_create_hook, pthread_create);
INTERPOSE(__pthread_join_hook, pthread_join);
INTERPOSE(__pthread_exit_hook, pthread_exit);
INTERPOSE(__pthread_self_hook, pthread_self);
INTERPOSE(__pthread_equal_hook, pthread_equal);
INTERPOSE(__pthread_kill_hook, pthread_kill);
INTERPOSE(__pthread_detach_hook, pthread_detach);
INTERPOSE(__pthread_sigmask_hook, pthread_sigmask);
INTERPOSE(__pthread_setschedparam_hook, pthread_setschedparam);
INTERPOSE(__pthread_getschedparam_hook, pthread_getschedparam);
INTERPOSE(__pthread_get_stackaddr_np_hook, pthread_get_stackaddr_np);
INTERPOSE(__pthread_get_stacksize_np_hook, pthread_get_stacksize_np);
INTERPOSE(__pthread_cancel_hook, pthread_cancel);
INTERPOSE(__pthread_main_thread_np_hook, pthread_main_thread_np);
INTERPOSE(__pthread_main_np_hook, pthread_main_np);

#endif // __PTHREAD_WRAPPERS_H__
