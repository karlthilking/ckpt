/* libckpt.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/writeckpt.h"
#include "xnd/vm_region.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/thread_info.h"
#include "xnd/shared_cache.h"
#include "xnd/util/debug.h"
#include "xnd/wrappers/file_wrappers.h"
#include "xnd/wrappers/signal_wrappers.h"

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>
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

void xnd_precheckpoint(void)
{
        sig_state_save();
        fd_table_save_state();
        _pthread_ptr_munge_token = thread_munge_token();
}

void xnd_postrestart(void)
{
        thread_sig_fixup(_pthread_ptr_munge_token);
        ckpt_vm_deallocate_regions();
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

/**
 * setup():
 *  Block SIGUSR2 process-wide so it only arrives for the
 *  checkpoint thread, then enable thread_handler to run
 *  on SIGUSR1 for user threads.
 */
__constructor() void xnd_setup(void)
{
        struct sigaction        sa;
        sigset_t                set;
        
        /* Every thread blocks SIGUSR2 except for checkpoint thread */
        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        pthread_sigmask(SIG_BLOCK, &set, NULL);
        
        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = thread_sighandler;
        sigaction(SIGUSR1, &sa, NULL);
        
        xnd_log_setup();
        fd_table_init();
        thread_list_init();
        set_xnd_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
        dump_debug_info();
#endif
}

__destructor() void xnd_cleanup(void)
{
        set_xnd_state(XND_EXITING);
        fd_table_destroy();
        thread_list_destroy();
        xnd_log_cleanup();
}
