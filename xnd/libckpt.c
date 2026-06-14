/* libckpt.c */
#include "xnd/xnd.h"
#include "xnd/ckpt.h"
#include "xnd/writeckpt.h"
#include "xnd/vm_region.h"
#include "xnd/pac.h"
#include "xnd/tls.h"
#include "xnd/thread_info.h"
#include "xnd/shared_cache.h"
#include "xnd/util/debug.h"
#include "xnd/wrappers/pthread_wrappers.h"
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

static _Atomic ckpt_state       libckpt_state = XND_UNINITIALIZED;
static uintptr_t                _pthread_ptr_munge_token;

ckpt_state get_ckpt_state(void)
{
        return atomic_load(&libckpt_state);
}

void set_ckpt_state(ckpt_state new)
{
        atomic_store(&libckpt_state, new);
}

void precheckpoint(void)
{
        sig_state_save();
        fd_table_save_state();
        _pthread_ptr_munge_token = thread_munge_token();
}

void postrestart(void)
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

void docheckpoint(ucontext_t *uctx)
{
        ckpt_header_t           headers[MAX_CKPT_HEADERS];
        ckpt_vm_region_t        regions[MAX_CKPT_VM_REGIONS];
        ckpt_metadata_t         meta;

        bzero(&meta, sizeof(meta));
        if (shared_cache_get_info(&meta.shared_cache_info) < 0) {
                xnd_warn("Failed to get dyld shared cache info,"
                         "aborting checkpoint...\n");
                return;
        }

        meta.nr_regions = ckpt_vm_save_regions(regions);
        if (unlikely(meta.nr_regions > MAX_CKPT_VM_REGIONS)) {
                xnd_error("Not enough space to save all memory regions!\n");
                return;
        }

        meta.nr_headers += meta.nr_regions;
        for (u32 i = 0; i < meta.nr_regions; i++) {
                headers[i] = CKPT_VM_REGION_HEADER;
        }
        
        headers[meta.nr_headers] = CKPT_CONTEXT_HEADER;
        meta.nr_contexts++;
        meta.nr_headers++;

        write_ckpt(&meta, headers, regions, uctx);
}       

/**
 * setup():
 *  Block SIGUSR2 process-wide so it only arrives for the
 *  checkpoint thread, then enable thread_handler to run
 *  on SIGUSR1 for user threads.
 */
__constructor() void setup()
{
        struct sigaction        sa;
        sigset_t                set;
        
        /* Every thread blocks SIGUSR2 except for checkpoint thread */
        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        sigprocmask(SIG_BLOCK, &set, NULL);
        
        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = thread_sighandler;
        sigaction(SIGUSR1, &sa, NULL);
        
        fd_table_init();
        thread_list_init();
        xnd_log_setup();
        set_ckpt_state(XND_RUNNING);

#if DEVELOPMENT || DEBUG
        xnd_log_shared_cache_info();
        xnd_log_main_thread_info();
        dump_debug_info();
#endif
}

__destructor() void cleanup()
{
        set_ckpt_state(XND_EXITING);
        fd_table_destroy();
        thread_list_destroy();
        xnd_log_cleanup();
}
