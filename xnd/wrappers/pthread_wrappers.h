/* pthread_wrappers.h */
#ifndef PTHREAD_WRAPPERS_H
#define PTHREAD_WRAPPERS_H
#include "xnd/inject.h"
#include <pthread.h>

#define PTHREAD_SELF_DISCRIMINATOR      0x5B9ULL
#define PTHREAD_MAGIC                   0x7770000000000000ULL
#define PTHREAD_TAG_MASK                0xFFF0000000000000ULL

extern void             *pthread_get_stackaddr_np(pthread_t);
extern size_t           pthread_get_stacksize_np(pthread_t);
extern pthread_t        pthread_main_thread_np(void);
extern int              pthread_main_np(void);
extern void             _pthread_set_self(pthread_t);

void    __pthread_cookie(void);
void    __pthread_slot_fixup(void);
void    __main_thread_postrestart_fixup(void);

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

#endif /* PTHREAD_WRAPPERS_H */
