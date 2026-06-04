/* exit_wrappers.h */
#ifndef __CKPT_EXIT_WRAPPERS_H__
#define __CKPT_EXIT_WRAPPERS_H__
#include <stdlib.h>
#include "inject.h"
#include "types.h"

void    __exit_hook(int);
void    __abort_hook(void);
u32     __arc4random_hook(void);
void    __arc4random_buf_hook(void *, size_t);
u32     __arc4random_uniform_hook(u32);

INTERPOSE(__exit_hook, exit);
INTERPOSE(__abort_hook, abort);
INTERPOSE(__arc4random_hook, arc4random);
INTERPOSE(__arc4random_buf_hook, arc4random_buf);
INTERPOSE(__arc4random_uniform_hook, arc4random_uniform);

#endif // __CKPT_EXIT_WRAPPERS_H__
