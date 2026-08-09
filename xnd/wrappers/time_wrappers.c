/* time_wrappers.c */
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "time_wrappers.h"
#include "xnd/interpose.h"

unsigned int __sleep_hook(unsigned int seconds)
{
	int err;
	struct timespec rm = { 0, 0 };
	struct timespec rq = { seconds, 0 };

	err = __nanosleep_hook(&rq, &rm);
	if (err != 0)
		return rm.tv_sec;

	return 0;
}

int __usleep_hook(useconds_t usec)
{
	long nsec;
	struct timespec rq;
	struct timespec rm = { 0, 0 };

	nsec = usec * NSEC_PER_USEC;
	rq.tv_sec = nsec / NSEC_PER_SEC;
	rq.tv_nsec = nsec % NSEC_PER_SEC;

	return __nanosleep_hook(&rq, &rm);
}

int __nanosleep_hook(const struct timespec *rqtp, struct timespec *rmtp)
{
	int err;
	long rq, rm;
	struct timeval tv;
	struct timespec start, end;

	rq = TIMESPEC_TO_NSEC(rqtp);
	if (rq < 0L || rq >= 100000000L) {
		errno = EINVAL;
		return -1;
	}

	TIMESPEC_TO_TIMEVAL(rqtp, &tv);
	if (rmtp != NULL)
		clock_gettime(CLOCK_MONOTONIC, &start);

	err = select(0, NULL, NULL, NULL, &tv);
	if (err != 0 && errno == EINTR && rmtp != NULL) {
		clock_gettime(CLOCK_MONOTONIC, &end);
		rm = rq - TIMESPEC_NSEC_DIFF(&end, &start);
		rmtp->tv_sec = rm / NSEC_PER_SEC;
		rmtp->tv_nsec = rm % NSEC_PER_SEC;
	}

	return err;
}

INTERPOSE(__sleep_hook, sleep);
INTERPOSE(__usleep_hook, usleep);
INTERPOSE(__nanosleep_hook, nanosleep);
