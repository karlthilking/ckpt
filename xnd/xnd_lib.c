/* libckpt.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/writeckpt.h"
#include "xnd/vm_region.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/thread_info.h"
#include "xnd/shared_cache.h"
#include "xnd/pid/pid.h"
#include "xnd/util/debug.h"
#include "xnd/platform/signal.h"
#include "xnd/pid/pid_table.h"
#include "xnd/wrappers/file_wrappers.h"
#include "xnd/wrappers/signal_wrappers.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include "xnd/coordinator/xnd_coord_client.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

static _Atomic enum xnd_state   libxnd_state = XND_UNINITIALIZED;
static uintptr_t                _pthread_ptr_munge_token;

enum xnd_state get_xnd_state(void)
{
        return atomic_load(&libxnd_state);
}

void set_xnd_state(enum xnd_state new_state)
{
        atomic_store(&libxnd_state, new_state);
}

void xnd_suspend_until_ckpt(void)
{
        struct xnd_msg  msg;
        int             err;
       
retry:
        err = recv_msg_from_coord(&msg);
        if (err != 0) {
                xnd_assert(errno == EINTR);
                goto retry;
        }

        xnd_assert(msg.hdr == XND_COMMAND);
        if (msg.cmd == XND_CKPT_CMD) {
                msg.hdr = XND_CLIENT_ACK;
                msg.cmd = XND_CKPT_CMD;
                xnd_assert(send_msg_to_coord(&msg) == 0);
        }
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
        connect_to_coord();
        register_with_coord_on_restart();
        
        thread_sig_fixup(_pthread_ptr_munge_token);
        ckpt_vm_deallocate_regions();
        pid_table_postrestart();
        fd_table_restore_state();
        xnd_log_setup();

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

        bzero(&header, sizeof(header));
        strcpy(header.magic, XND_HEADER_MAGIC);
        if (shared_cache_get_info(&header.shared_cache_info) < 0) {
                xnd_error("Failed to get dyld shared cache info\n");
                return;
        }

        header.pid = _real_getpid();
        header.ppid = _real_getppid();

        header.region_count = ckpt_vm_save_regions(regions);
        if (unlikely(header.region_count > XND_CKPT_VM_REGION_MAX)) {
                xnd_error("Not enough space to save all memory regions\n");
                return;
        }
        
        header.entry_count += header.region_count;
        for (u32 i = 0; i < header.region_count; i++)
                entries[i] = XND_VM_REGION_ENTRY;

        entries[header.entry_count] = XND_UCONTEXT_ENTRY;
        header.entry_count += 1;

        (void)write_ckpt(&header, entries, regions, uctx);
}

void xnd_atfork_prepare(void)
{
        /**
         * TODO
         * pid_table_atfork_prepare();
         * thread_list_atfork_prepare();
         */
}

void xnd_atfork_child(pid_t virt_pid, pid_t virt_ppid)
{
        /**
         * TODO
         * pid_table_atfork_child(virt_pid, virt_ppid);
         * thread_list_atfork_child();
         */
}

void xnd_atfork_parent(pid_t virt_cpid, pid_t real_cpid)
{
        /**
         * TODO
         * pid_table_atfork_parent(virt_cpid, real_cpid);
         * thread_list_atfork_parent();
         */
}

void xnd_atfork_failed(void)
{
        /**
         * TODO
         * pid_table_atfork_failed();
         * thread_list_atfork_failed();
         */
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
        int                     err;
        
        connect_to_coord();
        register_with_coord_on_launch();
        
        /* Every thread blocks SIGUSR2 except for checkpoint thread */
        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        err = pthread_sigmask(SIG_BLOCK, &set, NULL);
        if (err != 0) {
                xnd_error("pthread_sigmask: %s\n", strerror(err));
                xnd_abort();
        }
        
        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = thread_sighandler;
        err = __xnd_sigaction(SIGUSR1, &sa, NULL);
        if (err != 0) {
                xnd_error("__xnd_sigaction failed!\n");
                xnd_abort();
        }
        
        xnd_log_setup();
        fd_table_init();
        thread_list_init();
        pid_table_init();
        set_xnd_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
        dump_debug_info();
#endif
        xnd_trace("Returning from %s...\n", __func__);
}

static __destructor() void xnd_cleanup(void)
{
        set_xnd_state(XND_EXITING);
        fd_table_destroy();
        thread_list_destroy();
        pid_table_destroy();
        xnd_log_cleanup();

        send_exit_to_coord();
        disconnect_from_coord();
}
