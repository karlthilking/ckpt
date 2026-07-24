/* time_wrappers.h */
#ifndef TIME_WRAPPERS_H
#define TIME_WRAPPERS_H
#include "xnd/inject.h"
#include <time.h>
#include <unistd.h>

#define TIMESPEC_TO_TIMEVAL(__tsp, __tvp) do {                  \
        (__tvp)->tv_sec  = (__tsp)->tv_sec;                     \
        (__tvp)->tv_usec = ((__tsp)->tv_nsec / NSEC_PER_USEC);  \
} while (0)

#define TIMEVAL_TO_TIMESPEC(__tvp, __tsp) do {                  \
        (__tsp)->tv_sec  = (__tvp)->tv_sec;                     \
        (__tsp)->tv_nsec = ((__tvp)->tv_usec * NSEC_PER_USEC);  \
} while (0)

#define TIMESPEC_TO_NSEC(__tsp) \
        (((__tsp)->tv_sec * NSEC_PER_SEC) + (__tsp)->tv_nsec)

#define TIMESPEC_NSEC_DIFF(__first, __second) \
        (TIMESPEC_TO_NSEC((__first))) - (TIMESPEC_TO_NSEC((__second)))

#ifndef MSEC_PER_SEC
# define MSEC_PER_SEC    (1000L)
#endif

#ifndef NSEC_PER_USEC
# define NSEC_PER_USEC   (1000L)
#endif

#ifndef USEC_PER_MSEC
# define USEC_PER_MSEC   (1000L)
#endif

#ifndef NSEC_PER_MSEC
# define NSEC_PER_MSEC   (1000000L)
#endif

#ifndef USEC_PER_SEC
# define USEC_PER_SEC    (1000000L)
#endif

#ifndef NSEC_PER_SEC
# define NSEC_PER_SEC    (1000000000L)
#endif

unsigned int __sleep_hook(unsigned int);
int __usleep_hook(useconds_t);
int __nanosleep_hook(const struct timespec *, struct timespec *);

INTERPOSE(__sleep_hook, sleep)
INTERPOSE(__usleep_hook, usleep);
INTERPOSE(__nanosleep_hook, nanosleep)

#endif /* TIME_WRAPPERS_H */
