/* pid_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/inject.h"
#include "xnd/pid/pid_table.h"
#include "xnd/pid/pid_wrappers.h"
#include <unistd.h>
#include <sys/types.h>

pid_t __getpid_hook(void)
{
        return pid_table_getpid();
}

pid_t __getppid_hook(void)
{
        return pid_table_getppid();
}

pid_t __getpgrp_hook(void)
{
        pid_t real_pgrp, virt_pgrp;

        real_pgrp = _real_getpgrp();
        virt_pgrp = pid_table_real_to_virtual(real_pgrp);
                
        return virt_pgrp;
}

pid_t __getpgid_hook(pid_t pid)
{
        pid_t real_pgid, virt_pgid;

        real_pgid = _real_getpgid();
        virt_pgid = pid_table_real_to_virtual(real_pgid);

        return virt_pgid;
}

pid_t __fork_hook(void)
{
        pid_t child_pid, retval;
        
        unsafe_enter();
        xnd_atfork();
        child_pid = fork();

        switch (child_pid) {
        case -1:
                retval = -1;
                break;
        case 0:
                retval = 0;
                xnd_postfork_child();
        default:
                retval = child_pid;
                xnd_postfork_parent();
        }

        unsafe_exit();
        return retval;
}

pid_t __wait_hook(int *status)
{
        return __wait4_hook(-1, status, 0, NULL);
}

pid_t __waitpid_hook(pid_t pid, int *status, int options)
{
        return __wait4_hook(pid, status, options, NULL);
}

pid_t __wait3_hook(int *status, int options, struct rusage *rusage)
{
        return __wait4_hook(-1, status, options, rusage);
}

pid_t __wait4_hook(pid_t pid, int *status, int options, struct rusage *rusage)
{
        pid_t                   real_pid, real_ret, virt_ret;
        int                     stat, sv_errno;
        const struct timespec   ts = { .tv_sec = 0, .tv_nsec = 1000 };
        
        sv_errno = errno;
        if (!status) {
                status = &stat;
        }

again:
        real_pid = pid_table_virtual_to_real(pid);
        unsafe_enter();
        real_ret = wait4(real_pid, status, options | WNOHANG, rusage);
        if (real_ret <= 0) {
                virt_ret = real_ret;
                if (real_ret < 0) {
                        sv_errno = errno;
                }
        } else {
                virt_ret = pid_table_real_to_virtual(real_ret);
                if (WIFEXITED(stat) || WIFSIGNALED(stat)) {
                        pid_table_erase(virt_ret);
                }
        }
        unsafe_exit();

        if ((options & WNOHANG) || virt_ret != 0) {
                goto done;
        } else {
                nanosleep(&ts, NULL);
                goto again;
        }

done:
        errno = sv_errno;
        return virt_ret;
}

INTERPOSE(__getpid_hook, getpid);
INTERPOSE(__getppid_hook, getppid);
INTERPOSE(__getpgrp_hook, getpgrp);
INTERPOSE(__getpgid_hook, getpgid);
INTERPOSE(__fork_hook, fork);
INTERPOSE(__wait_hook, wait);
INTERPOSE(__waitpid_hook, waitpid);
INTERPOSE(__wait3_hook, wait3);
INTERPOSE(__wait4_hook, wait4);
INTERPOSE(__kill_hook, kill);

