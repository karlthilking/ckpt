/* pid_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/inject.h"
#include "xnd/thread_info.h"
#include "xnd/util/env.h"
#include "xnd/wrappers/time_wrappers.h"
#include "xnd/coordinator/xnd_coord_client.h"
#include "pid.h"
#include "pid_table.h"
#include "pid_table_common.h"
#include "pid_wrappers.h"
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

extern pid_t _virt_pid;
extern pid_t _virt_ppid;
extern pid_t _real_pid;
extern pid_t _real_ppid;

static pid_t virtual_to_real_pid(pid_t virt)
{
        pid_t real;

        pid_table_acquire();
        if ((real = pid_table_virtual_to_real(virt)) != -1) {
                goto out;
        }

        real = virt_to_real_pid_from_coord(virt);
        if (real == -1) {
                xnd_trace("Failed to get virtual -> real translation "
                          "(virtual pid: %d)\n", virt);
        }
out:
        pid_table_release();
        return real;
}

static pid_t real_to_virtual_pid(pid_t real)
{
        pid_t virt;

        pid_table_acquire();
        if ((virt = pid_table_real_to_virtual(real)) != -1) {
                goto out;
        }

        virt = real_to_virt_pid_from_coord(real);
        if (virt == -1) {
                xnd_trace("Failed to get real -> virtual translation "
                          "(real pid: %d)\n", real);
        }
out:
        pid_table_release();
        return virt;
}

pid_t __getpid_hook(void)
{
        if (_virt_pid == -1) {
                return _real_getpid();
        }

        return _virt_pid;
}

pid_t __getppid_hook(void)
{
        if (_virt_ppid == -1) {
                return _real_getppid();
        }

        return _virt_ppid; 
}

pid_t __getpgrp_hook(void)
{
        pid_t real_pgrp, virt_pgrp;
        
        unsafe_enter();
        real_pgrp = _real_getpgrp();

        if (real_pgrp == _real_pid) {
                virt_pgrp = _virt_pid;
        } else if (real_pgrp == _real_ppid) {
                virt_pgrp = _virt_ppid;
        } else {
                virt_pgrp = real_to_virtual_pid(real_pgrp);
        }
        
        unsafe_exit();
        return virt_pgrp;
}

pid_t __getpgid_hook(pid_t pid)
{
        pid_t real_pid, real_pgid, virt_pgid;
        
        if (pid == 0 || pid == _real_pid) {
                return __getpgrp_hook();
        }

        unsafe_enter();
        real_pid = virtual_to_real_pid(pid);
        if (real_pid == -1) {
                goto fail;
        }

        real_pgid = _real_getpgid(real_pid);
        virt_pgid = real_to_virtual_pid(real_pgid);

        unsafe_exit();
        return virt_pgid;
fail:
        unsafe_exit();
        return -1;
}

pid_t __fork_hook(void)
{
        pid_t virt_ret, real_ret;
        
        unsafe_enter();
        
        /**
         * Register atfork handlers if not already registered; prepare
         * and child responsibilities at the time of fork() are as follows:
         *
         * xnd_atfork_prepare():
         *  Connect to the coordinator for the child and inform the
         *  coordinator that a child is about to connect (coordinator will
         *  send the parent the child's virtual pid here). Then, environment
         *  variable "XND_VIRTUAL_PID" will be set to the child's virtual pid
         *  so parent can update the pid table mapping after fork (using
         *  env_get_pid_info()).
         *
         * xnd_atfork_child():
         *  Child will exchange handshake with the coordinator and receive
         *  its virtual pid. Then, the child will update its pid table
         *  with its new virtual -> real pid mapping.
         */
        xnd_register_fork_handlers();

        switch ((real_ret = fork())) {
        case -1:
                xnd_atfork_failed();
                virt_ret = -1;
                break;
        case 0:
                virt_ret = 0;
                break;
        default:
                env_get_pid_info(&virt_ret, NULL, NULL, NULL);
                pid_table_acquire();
                pid_table_update(virt_ret, real_ret);
                pid_table_release();
                break;
        }

        unsafe_exit();
        return virt_ret;
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
        pid_t           real_pid, real_ret, virt_ret;
        int             stat, sv_errno;
        struct timespec ts = { 0, 1000 };

        sv_errno = errno;
        if (!status) {
                status = &stat;
        }

        for (;;) {
                unsafe_enter();
                switch (pid) {
                case INT32_MIN ... -2:
                        real_pid = virtual_to_real_pid(-pid);
                        xnd_assert(real_pid != -1);
                        real_pid = -real_pid;
                        break;
                case 0:
                case -1:
                        real_pid = pid;
                        break;
                default:
                        real_pid = virtual_to_real_pid(pid);
                        xnd_assert(real_pid != -1);
                        break;
                }
                
                real_ret = wait4(real_pid, status, options | WNOHANG, ru);
                switch (real_ret) {
                case -1:
                        sv_errno = errno;
                        goto fail;
                case 0:
                        virt_ret = 0;
                        break;
                default:
                        virt_ret = real_to_virtual_pid(real_ret);
                        if (WIFEXITED(*status)) {
                                pid_table_acquire();
                                pid_table_erase(virt_ret);
                                pid_table_release();
                        } else if (WIFSIGNALED(*status)) {
                                real_pid = virtual_to_real_pid(virt_ret);
                                notify_coord_of_exit(real_pid);
                                pid_table_acquire();
                                pid_table_erase(virt_ret);
                                pid_table_release();
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
fail:
        errno = sv_errno;
        return -1;
}

int __kill_hook(pid_t pid, int sig)
{
        int     retval;
        pid_t   real_pid;

        if (unlikely(sig == env_get_ckpt_signal())) {
                xnd_warn("Signal %d is reserved\n", sig);
                return -1;
        }

        switch (pid) {
        case INT32_MIN ... -2:
                retval = __killpg_hook(-pid, sig);
                break;
        case -1:
                unsafe_enter();
                retval = kill(-1, sig);
                unsafe_exit();
                break;
        case 0:
                retval = __killpg_hook(0, sig);
                break;
        default:
                unsafe_enter();
                real_pid = virtual_to_real_pid(pid);
                if (real_pid == -1) {
                        retval = -1;
                } else {
                        retval = kill(real_pid, sig);
                }
                unsafe_exit();
                break;
        }

        return retval;
}

int __killpg_hook(pid_t pgrp, int sig)
{
        int     retval;
        pid_t   real_pgrp;
        
        if (sig == env_get_ckpt_signal()) {
                xnd_warn("Signal %d is reserved for xnd\n", sig);
                return -1;
        }

        unsafe_enter();
        if (pgrp == 0) {
                retval = killpg(0, sig);
        } else {
                real_pgrp = virtual_to_real_pid(pgrp);
                if (real_pgrp == -1) {
                        retval = -1;
                } else {
                        retval = killpg(real_pgrp, sig);
                }
        }
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

