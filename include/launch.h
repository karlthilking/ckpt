/* launch.h */
#ifndef __CKPT_LAUNCH_H__
#define __CKPT_LAUNCH_H__
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <spawn.h>
#include <dlfcn.h>
#include <assert.h>
#include "types.h"

#ifndef POSIX_SPAWN_DISABLE_ASLR
# define POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

void interactive(void);
void print(char *);
void checkpoint(char **);
void restart(char *);
void usage(void);

#endif // __CKPT_LAUNCH_H__
