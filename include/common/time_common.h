/* time_common.h */
#ifndef TIME_COMMON_H
#define TIME_COMMON_H

#define MSEC_PER_SEC    (1000L)
#define NSEC_PER_USEC   (1000L)
#define USEC_PER_MSEC   (1000L)
#define NSEC_PER_MSEC   (1000000L)
#define USEC_PER_SEC    (1000000L)
#define NSEC_PER_SEC    (1000000000L)

#define TIMEVAL_TO_MSEC(tvp) \
        (((tvp)->tv_sec * MSEC_PER_SEC) + ((tvp)->tv_usec / USEC_PER_MSEC))

#define TIMEVAL_MSEC_DIFF(a, b) \
        (TIMEVAL_TO_MSEC(a) - TIMEVAL_TO_MSEC(b))

#endif /* TIME_COMMON_H */
