/* pid_wrappers.h */
#ifndef PID_WRAPPERS_H
#define PID_WRAPPERS_H

#include <sys/types.h>
#include <sys/resource.h>
#include <libproc.h>

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

int __proc_pidinfo_hook(int, int, u64, void *, int);
int __proc_pidfdinfo_hook(int, int, int, void *, int);
int __proc_pidfileportinfo_hook(int, u32, int, void *, int);
int __proc_name_hook(int, void *, u32);
int __proc_regionfilename_hook(int, u64, void *, u32);
int __proc_pidpath_hook(int, void *, u32);
int __proc_pid_rusage_hook(int, int, rusage_info_t *);
int __proc_track_dirty_hook(pid_t, u32);
int __proc_set_dirty_hook(pid_t, bool);
int __proc_get_dirty_hook(pid_t, u32 *);
int __proc_clear_dirty_hook(pid_t, u32);
int __proc_terminate_hook(pid_t, int *);
int __proc_udata_info_hook(int, int, void *, int);
int __proc_listpgrppids_hook(pid_t, void *, int);
int __proc_listchildpids_hook(pid_t, void *, int);
int __proc_listpids_hook(u32, u32, void *, int);
int __proc_listallpids_hook(void *, int);

#endif /* PID_WRAPPERS_H */
