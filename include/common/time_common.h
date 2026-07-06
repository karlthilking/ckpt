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

#if TIMING
# define TIMER_PUSH(event)                      \
{                                               \
        struct timeval __start, __end;          \
        const char __e[] = #event;              \
        gettimeofday(&__start, NULL);
#else
# define TIMER_PUSH(event)
#endif

#if TIMING
# define TIMER_POP()                                            \
        gettimeofday(&__end, NULL);                             \
        xnd_printf("%s took: %ldms\n",                          \
                   __e, TIMEVAL_MSEC_DIFF(&__end, &__start));   \
}
#else
# define TIMER_POP()
#endif
                

#endif /* TIME_COMMON_H */
