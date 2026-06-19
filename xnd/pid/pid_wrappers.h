#ifndef PID_WRAPPERS_H
#define PID_WRAPPERS_H

#include <sys/types.h>
#include <sys/resource.h>

pid_t __getpid_hook(void);
pid_t __getppid_hook(void);
pid_t __getpgrp_hook(void);
pid_t __getpgid_hook(pid_t);

pid_t __fork_hook(void);
pid_t __wait_hook(int *);
pid_t __waitpid_hook(pid_t, int *, int);
pid_t __wait3_hook(int *, int, struct rusage *);
pid_t __wait4_hook(pid_t, int *, int, struct rusage *);

int __kill_hook(pid_t, int);
int __killpg_hook(pid_t, int);

#endif /* PID_WRAPPERS_H */
