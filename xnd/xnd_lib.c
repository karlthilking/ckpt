/* libckpt.c */
#include "xnd.h"
#include "xnd_lib.h"
#include "writeckpt.h"
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

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

static _Atomic enum xnd_state   libxnd_state = XND_UNINITIALIZED;
static bool                     xnd_atfork_registered = false;
static uintptr_t                _pthread_ptr_munge_token;

__hidden uuid_t xnd_uuid;
__hidden u32    xnd_pid;
__hidden u32    xnd_ppid;
__hidden u32    xnd_pgid;

__hidden u64    epoch           = 0;
__hidden u32    num_peers       = 0;
__hidden bool   is_root_of_tree = false;

enum xnd_state get_xnd_state(void)
{
        return atomic_load(&libxnd_state);
}

void set_xnd_state(enum xnd_state new_state)
{
        atomic_store(&libxnd_state, new_state);
}

void xnd_precheckpoint(void)
{
        sig_state_save();
        fd_table_save_state();
        _pthread_ptr_munge_token = thread_munge_token();
}

/**
 * xnd_postrestart:
 *  Re-connect and register with coordinator, restore auxiliary state.
 *  xnd_postrestart should only be called by the checkpoint thread.
 */
void xnd_postrestart(void)
{
        epoch++;
        connect_to_coord_on_restart();
        enter_coord_barrier(COORD_BARRIER_POSTRESTART);
        
        thread_sig_fixup(_pthread_ptr_munge_token);
        ckpt_vm_deallocate_regions();
        pid_table_postrestart();
        fd_table_restore_state();

#if DEBUG || DEVELOPMENT
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
#endif
}

void xnd_checkpoint(ucontext_t *uctx)
{
        struct xnd_ckpt_header  header;
        struct xnd_vm_region    regions[XND_CKPT_VM_REGION_MAX];
        enum xnd_ckpt_entry     entries[XND_CKPT_ENTRY_MAX];
        u32                     nr_regions, nr_entries;

        nr_regions = ckpt_vm_save_regions(regions);
        if (unlikely(nr_regions > XND_CKPT_VM_REGION_MAX)) {
                xnd_error("Max memory regions exceeded\n");
                return;
        }

        for (u32 i = 0; i < nr_regions; i++) {
                *(entries + i) = XND_VM_REGION_ENTRY;
        }

        *(entries + nr_regions) = XND_UCONTEXT_ENTRY;
        nr_entries = nr_regions + 1;

        xnd_ckptfile_write_header(&header, nr_regions, nr_entries,
                                  xnd_uuid, xnd_pid, xnd_ppid, xnd_pgid,
                                  num_peers, is_root_of_tree);
        write_ckpt(&header, entries, regions, uctx);
}

void xnd_atfork_prepare(void)
{
        xnd_trace("Called %s\n", __func__);

        coord_client_atfork_prepare();
        pid_table_atfork_prepare();
        thread_list_atfork_prepare();
}

void xnd_atfork_child(void)
{
        xnd_trace("Called %s\n", __func__);

        coord_client_atfork_child();
        pid_table_atfork_child();
        thread_list_atfork_child();

        xnd_trace("Returning from %s\n", __func__);
}

void xnd_atfork_parent(void)
{
        xnd_trace("Called %s\n", __func__);

        coord_client_atfork_parent();
        pid_table_atfork_parent();
        thread_list_atfork_parent();
}

void xnd_atfork_failed(void)
{
        coord_client_atfork_failed();
        pid_table_atfork_failed();
        thread_list_atfork_failed();
}

void xnd_register_fork_handlers(void)
{
        void (*prepare)(void), (*parent)(void), (*child)(void);

        if (xnd_atfork_registered) {
                return;
        }
        
        child = xnd_atfork_child;
        parent = xnd_atfork_parent;
        prepare = xnd_atfork_prepare;

        if (pthread_atfork(prepare, parent, child) != 0) {
                xnd_error("pthread_atfork: %s\n", strerror(errno));
                xnd_abort();
        }

        xnd_atfork_registered = true;
}

/**
 * setup():
 *  Block SIGUSR2 process-wide so it only arrives for the
 *  checkpoint thread, then enable thread_handler to run
 *  on SIGUSR1 for user threads.
 */
static __constructor(101) void xnd_setup(void)
{
        struct sigaction        sa;
        sigset_t                set;
        int                     err, sig;

        connect_to_coord_on_launch();

        sigfillset(&set);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = thread_sighandler;
        
        sig = env_get_ckpt_signal();
        err = __xnd_sigaction(sig, &sa, NULL);
        if (err != 0) {
                xnd_error("__xnd_sigaction failed!\n");
                xnd_abort();
        }
        
        fd_table_init();
        thread_list_init();

        pid_table_init();
        pid_table_init_pid_info();

        set_xnd_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
        dump_debug_info();
#endif
}

static __destructor() void xnd_cleanup(void)
{
        set_xnd_state(XND_EXITING);
        
        fd_table_destroy();
        thread_list_destroy();
        
        xnd_log_cleanup();
        disconnect_from_coord();
}
