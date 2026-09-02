/* pid_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/tls.h"
#include "xnd/interpose.h"
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
#include <libproc.h>
#include <sys/types.h>
#include <sys/wait.h>

extern pid_t _virt_pid;
extern pid_t _virt_ppid;
extern pid_t _real_pid;
extern pid_t _real_ppid;

static inline pid_t
virtual_to_real_pid(pid_t virt)
{
	pid_t real;

	if (virt == _virt_pid)
		return _real_pid;

	pid_table_acquire();
	real = pid_table_virtual_to_real(virt);
	if (real != -1)
		goto out;

	real = virt_to_real_pid_from_coord(virt);
	if (real == -1)
		xnd_error("failed to get real pid (virtual=%d)\n", virt);
out:
	pid_table_release();
	return real;
}

static inline pid_t
real_to_virtual_pid(pid_t real)
{
	pid_t virt;

	if (real == _real_pid)
		return _virt_pid;

	pid_table_acquire();
	virt = pid_table_real_to_virtual(real);
	if (virt != -1)
		goto out;

	virt = real_to_virt_pid_from_coord(real);
	if (virt == -1)
		xnd_error("failed to get virtual pid (real=%d)\n", real);

out:
	pid_table_release();
	return virt;
}

pid_t __getpid_hook(void)
{
	if (unlikely(_virt_pid == -1))
		return _real_getpid();

	return _virt_pid;
}

pid_t __getppid_hook(void)
{
	if (unlikely(_virt_ppid == -1))
		return _real_getppid();

	return _virt_ppid;
}

pid_t __getpgrp_hook(void)
{
        pid_t real_pgrp, virt_pgrp;

        unsafe_enter();
        real_pgrp = _real_getpgrp();

        if (real_pgrp == _real_pid)
                virt_pgrp = _virt_pid;
        else if (real_pgrp == _real_ppid)
                virt_pgrp = _virt_ppid;
        else
                virt_pgrp = real_to_virtual_pid(real_pgrp);

        unsafe_exit();
        return virt_pgrp;
}

pid_t __getpgid_hook(pid_t pid)
{
        pid_t real_pid, real_pgid, virt_pgid;

        if (pid == 0 || pid == _real_pid)
                return __getpgrp_hook();

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1)
                goto fail;

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

static inline void
bsdinfo_real_to_virt(struct proc_bsdinfo *bsd)
{
        pid_t virt;

        if ((virt = real_to_virtual_pid((pid_t)bsd->pbi_pid)) != -1)
                bsd->pbi_pid = (u32)virt;
        if ((virt = real_to_virtual_pid((pid_t)bsd->pbi_ppid)) != -1)
                bsd->pbi_ppid = (u32)virt;
        if ((virt = real_to_virtual_pid((pid_t)bsd->pbi_pgid)) != -1)
                bsd->pbi_pgid = (u32)virt;
        if ((virt = real_to_virtual_pid((pid_t)bsd->e_tpgid)) != -1)
                bsd->e_tpgid = (u32)virt;
}

static inline void
bsdshortinfo_real_to_virt(struct proc_bsdshortinfo *bsd)
{
        pid_t virt;

        if ((virt = real_to_virtual_pid((pid_t)bsd->pbsi_pid)) != -1)
                bsd->pbsi_pid = (u32)virt;
        if ((virt = real_to_virtual_pid((pid_t)bsd->pbsi_ppid)) != -1)
                bsd->pbsi_ppid = (u32)virt;
        if ((virt = real_to_virtual_pid((pid_t)bsd->pbsi_pgid)) != -1)
                bsd->pbsi_pgid = (u32)virt;
}

/**
 * pid_list_real_to_virt:
 *  Entries with no known virtual mapping (unrelated, non-virtualized
 *  processes) are left as their real value rather than overwritten.
 */
static inline void pid_list_real_to_virt(pid_t *list, int count)
{
        pid_t virt;

	for (int i = 0; i < count; i++) {
		if (list[i] == 0)
			continue;
		virt = real_to_virtual_pid(list[i]);
		if (virt != -1)
			list[i] = virt;
	}
}


int
__proc_pidinfo_hook(int pid, int flavor, u64 arg, void *buf, int bufsize)
{
	int ret;
	pid_t real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_pidinfo(pid, flavor, arg, buf, bufsize);

        unsafe_enter();
	real_pid = virtual_to_real_pid(pid);
	if (real_pid == -1) {
		unsafe_exit();
		errno = ESRCH;
		return 0;
	}

        ret = proc_pidinfo(real_pid, flavor, arg, buf, bufsize);
        if (ret > 0 && buf != NULL) {
                switch (flavor) {
                case PROC_PIDTBSDINFO: {
                        struct proc_bsdinfo *info;
                        if ((size_t)ret >= sizeof(*info)) {
                                info = (struct proc_bsdinfo *)buf;
                                bsdinfo_real_to_virt(info);
                        }
                        break;
                }
                case PROC_PIDTASKALLINFO: {
                        struct proc_taskallinfo *info;
                        if ((size_t)ret >= sizeof(*info)) {
                                info = (struct proc_taskallinfo *)buf;
                                bsdinfo_real_to_virt(&info->pbsd);
                        }
                        break;
                }
                case PROC_PIDT_SHORTBSDINFO: {
                        struct proc_bsdshortinfo *info;
                        if ((size_t)ret >= sizeof(*info)) {
                                info = (struct proc_bsdshortinfo *)buf;
                                bsdshortinfo_real_to_virt(info);
                        }
                        break;
                }
                default:
                        break;
                }
        }

        unsafe_exit();
        return ret;
}

int __proc_pidfdinfo_hook(int pid, int fd, int flavor,
                          void *buf, int bufsize)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_pidfdinfo(pid, fd, flavor, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_pidfdinfo(real_pid, fd, flavor, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_pidfileportinfo_hook(int pid, u32 fileport, int flavor,
                                void *buf, int bufsize)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_pidfileportinfo(
                        pid, fileport, flavor, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_pidfileportinfo(
                real_pid, fileport, flavor, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_name_hook(int pid, void *buf, u32 bufsize)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_name(pid, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_name(real_pid, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_regionfilename_hook(int pid, u64 addr, void *buf, u32 bufsize)
{

        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_regionfilename(pid, addr, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_regionfilename(real_pid, addr, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_pidpath_hook(int pid, void *buf, u32 bufsize)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_pidpath(pid, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_pidpath(real_pid, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_pid_rusage_hook(int pid, int flavor, rusage_info_t *buf)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_pid_rusage(pid, flavor, buf);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_pid_rusage(real_pid, flavor, buf);
        unsafe_exit();
        return ret;
}

int __proc_track_dirty_hook(pid_t pid, u32 flags)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_track_dirty(pid, flags);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_track_dirty(real_pid, flags);
        unsafe_exit();
        return ret;
}

int __proc_set_dirty_hook(pid_t pid, bool dirty)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_set_dirty(pid, dirty);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_set_dirty(real_pid, dirty);
        unsafe_exit();
        return ret;
}

int __proc_get_dirty_hook(pid_t pid, u32 *flags)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_get_dirty(pid, flags);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_get_dirty(real_pid, flags);
        unsafe_exit();
        return ret;
}

int __proc_clear_dirty_hook(pid_t pid, u32 flags)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_clear_dirty(pid, flags);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_clear_dirty(real_pid, flags);
        unsafe_exit();
        return ret;
}

int __proc_terminate_hook(pid_t pid, int *sig)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_terminate(pid, sig);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return -1;
        }

        ret = proc_terminate(real_pid, sig);
        unsafe_exit();
        return ret;
}

int __proc_udata_info_hook(int pid, int flavor, void *buf, int bufsize)
{
        int     ret;
        pid_t   real_pid;

        if (XND_SKIP_INTERPOSE())
                return proc_udata_info(pid, flavor, buf, bufsize);

        unsafe_enter();
        if ((real_pid = virtual_to_real_pid(pid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_udata_info(real_pid, flavor, buf, bufsize);
        unsafe_exit();
        return ret;
}

int __proc_listpgrppids_hook(pid_t pgrp, void *buf, int bufsize)
{
        int     ret;
        pid_t   real_pgrp;

        if (XND_SKIP_INTERPOSE())
                return proc_listpgrppids(pgrp, buf, bufsize);

        unsafe_enter();
        if ((real_pgrp = virtual_to_real_pid(pgrp)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_listpgrppids(real_pgrp, buf, bufsize);
        if (ret > 0 && buf != NULL)
                pid_list_real_to_virt(buf, ret / (int)sizeof(pid_t));

        unsafe_exit();
        return ret;
}

int __proc_listchildpids_hook(pid_t ppid, void *buf, int bufsize)
{
        int     ret;
        pid_t   real_ppid;

        if (XND_SKIP_INTERPOSE())
                return proc_listchildpids(ppid, buf, bufsize);

        unsafe_enter();
        if ((real_ppid = virtual_to_real_pid(ppid)) == -1) {
                unsafe_exit();
                errno = ESRCH;
                return 0;
        }

        ret = proc_listchildpids(real_ppid, buf, bufsize);
        if (ret > 0 && buf != NULL)
                pid_list_real_to_virt(buf, ret / (int)sizeof(pid_t));

        unsafe_exit();
        return ret;
}

int __proc_listpids_hook(u32 type, u32 typeinfo, void *buf, int bufsize)
{
        int     ret;
        pid_t   real_typeinfo;

        if (XND_SKIP_INTERPOSE())
                return proc_listpids(type, typeinfo, buf, bufsize);

        unsafe_enter();
        if (type == PROC_PGRP_ONLY || type == PROC_PPID_ONLY) {
                real_typeinfo = virtual_to_real_pid((pid_t)typeinfo);
                if (real_typeinfo == -1) {
                        unsafe_exit();
                        errno = ESRCH;
                        return 0;
                }
                typeinfo = (u32)real_typeinfo;
        }

        ret = proc_listpids(type, typeinfo, buf, bufsize);
        if (ret > 0 && buf != NULL)
                pid_list_real_to_virt(buf, ret / (int)sizeof(pid_t));

        unsafe_exit();
        return ret;
}

int __proc_listallpids_hook(void *buf, int bufsize)
{
        int ret;

        if (XND_SKIP_INTERPOSE())
                return proc_listallpids(buf, bufsize);

        unsafe_enter();
        ret = proc_listallpids(buf, bufsize);
        if (ret > 0 && buf != NULL)
                pid_list_real_to_virt(buf, ret / (int)sizeof(pid_t));

        unsafe_exit();
        return ret;
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

INTERPOSE(__proc_pidinfo_hook, proc_pidinfo);
INTERPOSE(__proc_pidfdinfo_hook, proc_pidfdinfo);
INTERPOSE(__proc_pidfileportinfo_hook, proc_pidfileportinfo);
INTERPOSE(__proc_name_hook, proc_name);
INTERPOSE(__proc_regionfilename_hook, proc_regionfilename);
INTERPOSE(__proc_pidpath_hook, proc_pidpath);
INTERPOSE(__proc_pid_rusage_hook, proc_pid_rusage);
INTERPOSE(__proc_track_dirty_hook, proc_track_dirty);
INTERPOSE(__proc_set_dirty_hook, proc_set_dirty);
INTERPOSE(__proc_get_dirty_hook, proc_get_dirty);
INTERPOSE(__proc_clear_dirty_hook, proc_clear_dirty);
INTERPOSE(__proc_terminate_hook, proc_terminate);
INTERPOSE(__proc_udata_info_hook, proc_udata_info);
INTERPOSE(__proc_listpgrppids_hook, proc_listpgrppids);
INTERPOSE(__proc_listchildpids_hook, proc_listchildpids);
INTERPOSE(__proc_listpids_hook, proc_listpids);
INTERPOSE(__proc_listallpids_hook, proc_listallpids);

