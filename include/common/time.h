/* time_common.h */
#ifndef TIME_COMMON_H
#define TIME_COMMON_H

#include <time.h>

#ifndef MSEC_PER_SEC
# define MSEC_PER_SEC (1000L)
#endif

#ifndef NSEC_PER_USEC
# define NSEC_PER_USEC (1000L)
#endif

#ifndef USEC_PER_MSEC
# define USEC_PER_MSEC (1000L)
#endif

#ifndef NSEC_PER_MSEC
# define NSEC_PER_MSEC (1000000L)
#endif

#ifndef USEC_PER_SEC
# define USEC_PER_SEC (1000000L)
#endif

#ifndef NSEC_PER_SEC
# define NSEC_PER_SEC (1000000000L)
#endif

#define TIMEVAL_TO_MSEC(tvp) \
        (((tvp)->tv_sec * MSEC_PER_SEC) + ((tvp)->tv_usec / USEC_PER_MSEC))
#define TIMEVAL_TO_USEC(tvp) \
	(((tvp)->tv_sec * USEC_PER_SEC) + ((tvp)->tv_usec))
#define TIMEVAL_TO_NSEC(tvp) \
	(((tvp)->tv_sec * NSEC_PER_SEC) + ((tvp)->tv_usec * NSEC_PER_USEC))

#define TIMEVAL_MSEC_DIFF(end, begin) \
        (TIMEVAL_TO_MSEC(end) - TIMEVAL_TO_MSEC(begin))
#define TIMEVAL_USEC_DIFF(end, begin) \
	(TIMEVAL_TO_USEC(end) - TIMEVAL_TO_USEC(begin))
#define TIMEVAL_NSEC_DIFF(end, begin) \
	(TIMEVAL_TO_NSEC(end) - TIMEVAL_TO_NSEC(begin))

#define TIMESPEC_TO_MSEC(tsp) \
	(((tsp)->tv_sec * MSEC_PER_SEC) + ((tsp)->tv_nsec / NSEC_PER_MSEC))
#define TIMESPEC_TO_USEC(tsp) \
	(((tsp)->tv_sec * USER_PER_SEC) + ((tsp)->tv_nsec / NSEC_PER_USEC))
#define TIMESPEC_TO_NSEC(tsp) \
	(((tsp)->tv_sec * NSEC_PER_SEC) + ((tsp)->tv_nsec))

#define TIMESPEC_MSEC_DIFF(end, begin) \
	(TIMESPEC_TO_MSEC(end) - TIMESPEC_TO_MSEC(begin))
#define TIMESPEC_USEC_DIFF(end, begin) \
	(TIMESPEC_TO_USEC(end) - TIMESPEC_TO_USEC(begin))
#define TIMESPEC_NSEC_DIFF(end, begin) \
	(TIMESPEC_TO_NSEC(end) - TIMESPEC_TO_NSEC(begin))

#ifndef TIMESPEC_TO_TIMEVAL
# define TIMESPEC_TO_TIMEVAL(tsp, tvp)				   \
	do {							   \
		(tvp)->tv_sec = (tsp)->tv_sec;			   \
		(tvp)->tv_usec = ((tsp)->tv_nsec / NSEC_PER_USEC); \
	} while (0)
#endif /* TIMESPEC_TO_TIMEVAL */

#ifndef TIMEVAL_TO_TIMESPEC
# define TIMEVAL_TO_TIMESPEC(tvp, tsp)				   \
	do {							   \
		(tsp)->tv_sec = (tvp)->tv_sec;			   \
		(tsp)->tv_nsec = ((tvp)->tv_usec * NSEC_PER_USEC); \
	} while (0)
#endif /* TIMEVAL_TO_TIMESPEC */

/*
 * Convenience macros for timing certain events; TIMER_PUSH and TIMER_POP
 * must be placed in the same lexical scope.
 */
#if TIMING
# define TIMER_PUSH(name)				\
{							\
	struct timespec __tp_start, __tp_end;		\
	const char *__event = #name;			\
	long __elapsed;					\
	clock_gettime(CLOCK_UPTIME_RAW, &__tp_start);
# define TIMER_POP()						\
	clock_gettime(CLOCK_UPTIME_RAW, &__tp_end);		\
	__elapsed = TIMESPEC_MSEC_DIFF(&__tp_end, &__tp_start); \
	xnd_printf("%s took %ldms\n", __event, __elapsed);	\
}
#else /* !TIMING */
# define TIMER_PUSH(name) ((void)0)
# define TIMER_POP() ((void)0)
#endif /* TIMING */

#endif /* TIME_COMMON_H */
