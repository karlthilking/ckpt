/* libckpt.c */
#include "xnd.h"
#include "xnd_lib.h"
#include "writeckpt.h"
#include "ckptfile.h"
#include "vm_region.h"
#include "pac.h"
#include "tls.h"
#include "thread_info.h"
#include "shared_cache.h"
#include "pid/pid.h"
#include "util/debug.h"
#include "util/env.h"
#include "platform/signal.h"
#include "pid/pid_table.h"
#include "pid/pid_table_common.h"
#include "wrappers/file_wrappers.h"
#include "wrappers/signal_wrappers.h"
#include "coordinator/xnd_coord_api.h"
#include "coordinator/xnd_coord_client.h"

#include <mach/mach.h>
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

static void xnd_setup(void) __constructor(101);
static void xnd_cleanup(void) __destructor();

__private_extern enum xnd_state libxnd_state = XND_UNINITIALIZED;

__private_extern u32 xnd_pid;
__private_extern u32 xnd_ppid;
__private_extern u32 xnd_pgid;
__private_extern uuid_t xnd_uuid;

__private_extern u64 epoch = 0;
__private_extern u32 num_peers = 0;
__private_extern bool is_root_of_tree = false;

enum xnd_state
get_xnd_state(void)
{
	return __atomic_load_n(&libxnd_state, __ATOMIC_ACQUIRE);
}

void
set_xnd_state(enum xnd_state next)
{
	__atomic_store_n(&libxnd_state, next, __ATOMIC_RELEASE);
}

void xnd_precheckpoint(void)
{
        sig_state_save();
        fd_table_save();

	if (thread_ptr_munge_save() != 0)
		xnd_warn("warning: possible tls/thread corruption\n");
}

void xnd_postcheckpoint(void)
{
        epoch++;
}

/*
 * xnd_postrestart_early:
 *  Re-connect and re-register with the coordinator, and restore
 *  any auxiliary state (signals, open files, etc.).
 */
void xnd_postrestart_early(void)
{
        int dirfd;

	/*
	 * Any function that may call into libpthread directly or
	 * indirectly will crash if we have not yet patched the
	 * checkpoint thread's signature and munge token, so
	 * it is safest to do this right away.
	 */
	thread_ptr_munge_fixup();

        epoch++;
        connect_to_coord_on_restart();
        enter_coord_barrier(COORD_BARRIER_POSTRESTART);

        fd_table_restore();
        sig_state_restore();

        /*
         * If using compressed checkpoints, then we had to inflate
         * the most recent checkpoint when we restarted. This is safe
         * to remove now as the compressed version is still on disk.
         */
        if (env_use_zlib_compression()) {
                dirfd = xnd_ckptdir_open(xnd_uuid, epoch - 1);
                if (dirfd != -1)
                        xnd_ckptfile_unlinkat(dirfd, xnd_pid);
        }

#if DEBUG || DEVELOPMENT
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
#endif
}

/*
 * xnd_postrestart_late:
 *  Finish any post-restart tasks that should happen after
 *  the checkpoint thread has done some other preparation, but
 *  before user threads are resumed.
 *
 *  Any functions that may call into libpthread or access thread
 *  local-variables should be put here, as the checkpoint thread
 *  does not restore/fixup its TCB and thread-local storage until
 *  after xnd_postrestart_early is called.
 */
void xnd_postrestart_late(void)
{
	pid_table_postrestart();
}

void
xnd_checkpoint(ucontext_t *uctx)
{
	static enum xnd_ckpt_entry entries[XND_CKPT_ENTRY_MAX];
	static struct xnd_vm_region regions[XND_CKPT_VM_REGION_MAX];
	static struct xnd_ckpt_header header;

	int ret;
	u32 idx, nregions, nentries;

	uuid_copy(header.xnd_uuid, xnd_uuid);
	header.xnd_pid = xnd_pid;
	header.xnd_ppid = xnd_ppid;
	header.xnd_pgid = xnd_pgid;
	header.num_peers = num_peers;
	header.is_root_of_tree = (u32)is_root_of_tree;

	nregions = ckpt_vm_save_regions(regions);
	if (nregions > XND_CKPT_VM_REGION_MAX) {
		xnd_error("max memory regions exceeded: %u\n", nregions);
		return;
	} else if (nregions == 0) {
		xnd_error("ckpt_vm_save_regions saved 0 regions\n");
		return;
	}

	idx = 0;
	while (idx < nregions)
		entries[idx++] = XND_VM_REGION_ENTRY;

	entries[idx++] = XND_UCONTEXT_ENTRY;
	nentries = idx;

	header.entry_count = nentries;
	header.region_count = nregions;

	/*
	 * xnd_ckptfile_write_header will write the remaining fields
	 * of the checkpoint header that were not filled in here.
	 */
	xnd_ckptfile_write_header(&header);
	ret = write_ckpt(&header, entries, regions, uctx);
	if (ret != 0)
		xnd_error("failed to write checkpoint file\n");

	bzero(&entries, sizeof(entries));
	bzero(&regions, sizeof(regions));
	bzero(&header, sizeof(header));
}

void xnd_atfork_prepare(void)
{
	set_xnd_state(XND_ATFORK);
        coord_client_atfork_prepare();
        pid_table_atfork_prepare();
        thread_list_atfork_prepare();

        if (env_get_dyld_shared_region()) {
                env_unset_dyld_shared_region();
        }
}

void xnd_atfork_child(void)
{
        coord_client_atfork_child();
        pid_table_atfork_child();
        thread_list_atfork_child();
	set_xnd_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_mach_port_info();
        xnd_log_shared_cache_info();
#endif
}

void xnd_atfork_parent(void)
{
        coord_client_atfork_parent();
        pid_table_atfork_parent();
        thread_list_atfork_parent();
	set_xnd_state(XND_RUNNING);
}

void xnd_atfork_failed(void)
{
        coord_client_atfork_failed();
        pid_table_atfork_failed();
        thread_list_atfork_failed();
	set_xnd_state(XND_RUNNING);
}

void xnd_register_fork_handlers(void)
{
	int err;
	void (*child)(void), (*parent)(void), (*prepare)(void);
	static bool xnd_atfork_registered = false;

	if (xnd_atfork_registered)
		return;

	child = xnd_atfork_child;
	parent = xnd_atfork_parent;
	prepare = xnd_atfork_prepare;

	err = pthread_atfork(prepare, parent, child);
	if (err != 0) {
		xnd_error("pthread_atfork: %s\n", strerror(err));
		xnd_abort();
	}

	xnd_atfork_registered = true;
}

static void
xnd_setup(void)
{
	int ret, sig;
	char *tmp;
        struct sigaction sa;

        connect_to_coord_on_launch();

	sigfillset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sa.sa_sigaction = thread_sighandler;

	sig = env_get_ckpt_signal();
	ret = xnd_sigaction(sig, &sa, NULL);
	if (ret != 0) {
		xnd_perror("xnd_sigaction");
		xnd_abort();
	}

        fd_table_init();
        thread_list_init();

        pid_table_init();
        pid_table_init_pid_info();

	tmp = env_get_tmp_binary();
	if (tmp != NULL && env_unlink_tmp_at_init())
		unlink(tmp);

        set_xnd_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_mach_port_info();
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
        dump_debug_info();
#endif
}

static void
xnd_cleanup(void)
{
	char *tmp;

        set_xnd_state(XND_EXITING);

        fd_table_destroy();
        thread_list_destroy();

	tmp = env_get_tmp_binary();
	if (tmp != NULL && env_unlink_tmp_at_exit())
		unlink(tmp);

        xnd_log_cleanup();
        disconnect_from_coord();
}
