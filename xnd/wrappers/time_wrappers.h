/* time_wrappers.h */
#ifndef TIME_WRAPPERS_H
#define TIME_WRAPPERS_H

#include <time.h>
#include <unistd.h>

#include "common/time.h"

unsigned int __sleep_hook(unsigned int);
int __usleep_hook(useconds_t);
int __nanosleep_hook(const struct timespec *, struct timespec *);

#endif /* TIME_WRAPPERS_H */
