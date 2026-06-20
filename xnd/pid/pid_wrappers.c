/* pid_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/inject.h"
#include "xnd/thread_info.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include "xnd/pid/pid_wrappers.h"
#include "xnd/wrappers/time_wrappers.h"
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

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
        pid_t real_pid, real_pgid, virt_pgid;

        if (pid == 0 || pid == pid_table_getpid()) {
                return __getpgrp_hook();
        }

        unsafe_enter();
        real_pid = pid_table_virtual_to_real(pid);
        if (unlikely(real_pid == -1)) {
                goto esrch;
        }

        real_pgid = _real_getpgid(real_pid);
        virt_pgid = pid_table_real_to_virtual(real_pgid);

        unsafe_exit();
        return virt_pgid;
esrch:
        unsafe_exit();
        errno = ESRCH;
        return -1;
}

pid_t __fork_hook(void)
{
        pid_t real_cpid, virt_cpid, virt_pid, retval;

        unsafe_enter();
        virt_pid = pid_table_getpid();

        pid_table_acquire();
        virt_cpid = pid_table_next_virtual();
        pid_table_release();

        xnd_atfork_prepare();
        real_cpid = fork();

        switch (real_cpid) {
        case -1:
                xnd_atfork_failed();
                retval = -1;
                break;
        case 0:
                xnd_atfork_child(virt_cpid, virt_pid);
                retval = 0;
                break;
        default:
                xnd_atfork_parent(virt_cpid, real_cpid);
                retval = virt_cpid;
                break;
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

pid_t __wait4_hook(pid_t pid, int *status, int options, struct rusage *ru)
{
        pid_t                   real_pid, real_ret, virt_ret;
        int                     stat, sv_errno;
        const struct timespec   ts = { .tv_sec = 0, .tv_nsec = 1000 };
        
        sv_errno = errno;
        if (!status) {
                status = &stat;
        }

        for (;;) {
                unsafe_enter();
                real_pid = pid_table_virtual_to_real(pid);
                if (unlikely(real_pid == -1)) {
                        xnd_warn("No virtual to real translation for "
                                 "virtual pid: %d\n", pid);
                        sv_errno = ECHILD;
                        virt_ret = -1;
                        unsafe_exit();
                        break;
                }

                real_ret = wait4(real_pid, status, options | WNOHANG, ru);
                switch (real_ret) {
                case -1:
                        virt_ret = -1;
                        sv_errno = errno;
                        break;
                case 0:
                        virt_ret = 0;
                        break;
                default:
                        virt_ret = pid_table_real_to_virtual(real_ret);
                        if (WIFEXITED(*status) || WIFSIGNALED(*status)) {
                                pid_table_erase(virt_ret);
                        }
                        break;
                }

                unsafe_exit();
                if ((options & WNOHANG) || virt_ret != 0) {
                        break;
                }
                __nanosleep_hook(&ts, NULL);
        }

        errno = sv_errno;
        return virt_ret;
}

int __kill_hook(pid_t pid, int sig)
{
        int     retval;
        pid_t   real_pid;
        
        if (unlikely(sig == SIGUSR1 || sig == SIGUSR2)) {
                /*** xnd reserved signal ***/
        }

        /**
         * if pid > 0, sig is sent to process whose id = pid
         * if pid == 0, sig is sent to pgrp of sender
         * if pid == -1, sig is sent to processes whose uid = getuid()
         *  - or if user == super-user, all processes excluding system
         *    processes
         * if pid < -1, sig is sent to all processes whose pgid = abs(pid)
         *
         * Therefore, only perform virtual -> real translation if
         * pid > 0. For pid == 0 or pid < -1, kill can be translated to
         * killpg(-pid, sig). For pid == -1, no translation is needed.
         */
        if (pid == 0 || pid < -1) {
                return __killpg_hook(-pid, sig);
        } else if (pid == -1) {
                unsafe_enter();
                retval = kill(-1, sig);
                unsafe_exit();
                return retval;
        }

        unsafe_enter();
        real_pid = pid_table_virtual_to_real(pid);
        if (real_pid == -1) {
                retval = -1;
        } else {
                retval = kill(real_pid, sig);
        }
        unsafe_exit();

        return retval;
}

int __killpg_hook(pid_t pgrp, int sig)
{
        int     retval;
        pid_t   real_pgrp;

        if (unlikely(sig == SIGUSR1 || sig == SIGUSR2)) {
                /*** xnd reserved signal ***/
                return -1;
        }

        unsafe_enter();
        if (pgrp == 0) {
                real_pgrp = _real_getpgrp();
        } else {
                real_pgrp = pid_table_virtual_to_real(pgrp);
        }
        retval = killpg(real_pgrp, sig);
        unsafe_exit();

        return retval;
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
INTERPOSE(__killpg_hook, killpg);

