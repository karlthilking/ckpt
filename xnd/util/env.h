/* env.h */
#ifndef XND_ENV_H
#define XND_ENV_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define XND_VIRTUAL_PID_ENV     "XND_VIRTUAL_PID"
#define XND_REAL_PID_ENV        "XND_REAL_PID"
#define XND_VIRTUAL_PPID_ENV    "XND_VIRTUAL_PPID"
#define XND_REAL_PPID_ENV       "XND_REAL_PPID"

#define XND_PROGRAM_ENV         "XND_PROGRAM"
#define XND_CKPT_SIGNAL_ENV     "XND_CKPT_SIGNAL"

void env_set_pid_info(pid_t, pid_t, pid_t, pid_t);
void env_get_pid_info(pid_t *, pid_t *, pid_t *, pid_t *);

void env_set_program_name(char *);
char *env_get_program_name(void);

void env_set_ckpt_signal(int);
int env_get_ckpt_signal(void);

#endif /* XND_ENV_H */
