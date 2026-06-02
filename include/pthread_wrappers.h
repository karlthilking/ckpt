/* pthread_wrappers.h */
#ifndef __PTHREAD_WRAPPERS_H__
#define __PTHREAD_WRAPPERS_H__
#include <pthread.h>
#include "inject.h"

#define PTHREAD_SELF_DISCRIMINATOR      0x5b9
#define PTHREAD_SELF_TLS_OFFSET         -224

#define _PTHREAD_STRUCT_DIRECT_THREADID_OFFSET          -8
// #define _PTHREAD_STRUCT_DIRECT_TSD_OFFSET               224
// #define _PTHREAD_STRUCT_DIRECT_STACKADDR_OFFSET         -48
// #define _PTHREAD_STRUCT_DIRECT_STACKBOTTOM_OFFSET       -40
// 
// #define _PTHREAD_SIG                                    0x54485244
// #define _PTHREAD_MUTEX_SIG                              0x4D555458
// #define _PTHREAD_COND_SIG_pristine                      0x434F4E44
// #define _PTHREAD_COND_SIG_psynch                        0x434F4E45
// #define _PTHREAD_COND_SIG_ulock                         0x434F4E46
// 
// #define PTHREAD_T_OFFSET                                (12*1024)
// 
// #define _EXTERNAL_POSIX_THREAD_KEYS_MAX                 512
// #define _INTERNAL_POSIX_THREAD_KEYS_MAX                 256
// #define _INTERNAL_POSIX_THREAD_KEYS_END                 768

extern int      pthread_create(pthread_t *, const pthread_attr_t *,
                               void *(*)(void *), void *);
extern void     pthread_exit(void *);
extern int      pthread_join(pthread_t, void **);

void    __pthread_cookie(void);
void    __pthread_slot_fixup(void);
int     __pthread_create_hook(pthread_t *, const pthread_attr_t *,
                              void *(*)(void *), void *);
int     __pthread_join_hook(pthread_t, void **);
void    __pthread_exit_hook(void *);

INTERPOSE(__pthread_create_hook, pthread_create);
INTERPOSE(__pthread_join_hook, pthread_join);
INTERPOSE(__pthread_exit_hook, pthread_exit);

#endif // __PTHREAD_WRAPPERS_H__
