/* libckpt.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include "ckpt.h"
#include "writeckpt.h"
#include "vm_region.h"
#include "pac.h"
#include "pthread_wrappers.h"
#include "file_wrappers.h"
#include "signal_wrappers.h"
#include "thread_info.h"

extern uintptr_t __stack_chk_guard;

void precheckpoint(void)
{
#if defined(__arm64e__)
        __pthread_cookie();
#endif
        sig_state_save();
        fd_table_save_state();
}

void postrestart(void)
{
        ckpt_vm_deallocate_regions();
#if defined(__arm64e__)
        __pthread_slot_fixup();
#endif
        sig_state_restore();
        fd_table_restore_state();
}

void docheckpoint(ucontext_t *uctx)
{
        ckpt_header_t           headers[MAX_CKPT_HEADERS];
        ckpt_vm_region_t        regions[MAX_CKPT_VM_REGIONS];
        ckpt_metadata_t         meta;

        bzero(&meta, sizeof(meta));
        if (shared_cache_get_info(&meta.shared_cache_info) < 0) {
                fprintf(stderr,
                        "Failed to get shared cache info, "
                        "aborting checkpoint...\n");
                return;
        }

        meta.nr_regions = ckpt_vm_save_regions(regions);
        meta.nr_headers += meta.nr_regions;
        for (u32 i = 0; i < meta.nr_regions; i++)
                headers[i] = CKPT_VM_REGION_HEADER;
        
        headers[meta.nr_headers] = CKPT_CONTEXT_HEADER;
        meta.nr_contexts = 1;
        meta.nr_headers++;

        write_ckpt(&meta, headers, regions, uctx);
}       

/**
 * setup():
 *  Block SIGUSR2 process-wide so it only arrives for the
 *  checkpoint thread, then enable thread_handler to run
 *  on SIGUSR1 for user threads.
 */
__constructor void setup()
{
        struct sigaction        sa;
        sigset_t                set;
        
        /* Every thread blocks SIGUSR2 except for checkpoint thread */
        sigemptyset(&set);
        sigaddset(&set, SIGUSR2);
        sigprocmask(SIG_BLOCK, &set, NULL);
        
        sigfillset(&sa.sa_mask);
        sa.sa_flags     = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = thread_sighandler;
        sigaction(SIGUSR1, &sa, NULL);
}

__destructor void cleanup()
{
        return;
}
