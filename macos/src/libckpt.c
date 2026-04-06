/* libckpt.c */
#define _XOPEN_SOURCE
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/sysctl.h>
#include <ucontext.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "../include/ckpt.h"

/**
 * Globals
 * @va_bits: Size of the virtual address space
 * @va_mask: Mask of unused bits in a typical virtual address
 */
__attribute__((visibility("hidden"))) u32 va_bits;
__attribute__((visibility("hidden"))) u64 va_mask;
__attribute__((visibility("hidden"))) struct sigaction sa;

/**
 * mem_rgn_valid: Determine if a given memory region should
 *                be saved at checkpoint
 *
 * @info:         Information about the memory region
 * @addr:         Start address of the memory region
 * @size:         Size of the memory region
 * return:        Returns 0 if region is invalid, 1 otherwise
 */
int mem_rgn_valid(vm_region_submap_info_data_64_t *info,
                  mach_vm_address_t addr, mach_vm_size_t size)
{
        /**
         * Avoid checkpointing the following memory regions:
         * commpage:
         * pagezero: 
         *  Sentinel zero page with no access permissions
         * dyld shared cache:
         *  kernel-managed cache for system libraries mapped by
         *  kernel into each process
         */
        if (COMMPAGE(addr, size) || PAGEZERO(addr, size) ||
            DYLD_SH_CH(addr, size))
                return 0;
        
        /**
         * Skip checkpointing any guard pages, i.e. memory 
         * regions that are not accessible by the current 
         * process
         */
        if (info->protection == VM_PROT_NONE)
                return 0;

        switch (info->user_tag) {
                case VM_MEMORY_MALLOC:
                case VM_MEMORY_MALLOC_NANO:
                case VM_MEMORY_MALLOC_TINY:
                case VM_MEMORY_MALLOC_SMALL:
                case VM_MEMORY_MALLOC_MEDIUM:
                case VM_MEMORY_MALLOC_LARGE:
                case VM_MEMORY_MALLOC_HUGE:
                case VM_MEMORY_MALLOC_LARGE_REUSABLE:
                case VM_MEMORY_MALLOC_LARGE_REUSED:
                        /* Heap data, allocation metadata, etc. */
                        assert(info->protection ||
                               info->max_protection);
                        return 1;
                case VM_MEMORY_REALLOC:
                        /* kernel memory */
                        return 0;
                case VM_MEMORY_STACK:
                        assert(info->protection || 
                               info->max_protection);
                        return 1;
                case VM_MEMORY_GUARD:
                        /* Regular guard page */
                        return 0;
                case VM_MEMORY_DYLD:
                case VM_MEMORY_DYLD_MALLOC:
                        /* Dynamic loader memory */
                        return 0;
                case VM_MEMORY_SHARED_PMAP:
                        /* commpage */
                        return 0;
                case VM_MEMORY_DYLIB:
                        /* Dynamic library text, data, etc. */
                        return 1;
                default:
                        break;
        }

        if (info->protection == VM_PROT_READ &&
            (info->share_mode == SM_SHARED ||
             info->share_mode == SM_TRUESHARED))
                return 0;
        
        return 1;
}

/**
 * get_mem_rgns: Obtain metadata about each memory region in
 *               the checkpointed process' virtual address space
 *
 * @rgns:        The memory region structure to save the
 *               metadata to
 * return:       Returns the number of regions that were saved
 */
int get_mem_rgns(mem_rgn_t *rgns)
{
        int                             nr_rgns = 0;
        mach_vm_address_t               addr    = 0;
        mach_vm_size_t                  size    = 0;
        natural_t                       depth   = 0;
        vm_region_submap_info_data_64_t info;
        mach_msg_type_number_t          count;
        kern_return_t                   kr;

        for (;;) {
                /**
                 * Count is an in-out parameter and so it must
                 * be reset each time before calling
                 * mach_vm_region_recurse to get the next memory
                 * region
                 */
                 count = VM_REGION_SUBMAP_INFO_COUNT_64;
                 kr = mach_vm_region_recurse(
                        mach_task_self(),
                        &addr,
                        &size,
                        &depth,
                        (vm_region_recurse_info_t)&info,
                        &count
                );

                if (kr != KERN_SUCCESS)
                        break;
                
                /**
                 * Skip over memory regions which should not
                 * be checkpointed
                 */
                if (!mem_rgn_valid(&info, addr, size)) {
                        addr += size;
                        continue;
                }
                
                if (info.is_submap) {
                        depth++;
                        continue;
                }
                
                rgns[nr_rgns].start    = addr;
                rgns[nr_rgns].size     = size;
                rgns[nr_rgns].end      = addr + size;
                rgns[nr_rgns].prot     = info.protection;
                rgns[nr_rgns].max_prot = info.max_protection;
                rgns[nr_rgns].mode     = info.share_mode;
                rgns[nr_rgns].tag      = info.user_tag;

                addr += size;
                nr_rgns++;
        }
        return nr_rgns;
}

/**
 * write_data:  Write generic data do a file
 *
 * @fd:         The file descriptor to use for writing
 * @data:       The data to write to the file
 * @size:       The size in bytes of the data to be written
 * return:      Returns -1 on failure, 0 on success
 */
int write_data(int fd, void *data, size_t size)
{
        size_t  bytes;
        ssize_t rc;

        for (bytes = 0; bytes < size && rc != -1; bytes += rc)
                rc = write(fd, data + bytes, size - bytes);
        
        if (rc < 0)
                perror("[Error] write");

        return (bytes == size) ? 0 : -1;
}

/**
 * write_ckpt:  Write the checkpoint file to disk
 *
 * @nr_hdrs:    The number of total checkpoint headers with
 *              corresponding data to write to the checkpoint file
 * @hdrs:       Checkpoint headers describing the data saved in the
 *              checkpoint file
 * @rgns:       Memory regions to save in the checkpoint file
 * @ctx:        Register context to save to the checkpoint file
 * @frames:     Call frame information for any call frames that
 *              were found to contain PAC signatures
 * return:      Returns -1 on failure, 0 on success
 */
int write_ckpt(int nr_hdrs, int nr_rgns, 
               int nr_ctxs, int nr_frames, 
               ckpt_hdr_t *hdrs, mem_rgn_t *rgns,
               reg_ctx_t *ctxs, callframe_t *frames)
{
        assert(nr_hdrs == nr_rgns + nr_ctxs + nr_frames);

        int  fd;
        int  wr_hdrs = 0, wr_rgns = 0, wr_ctxs = 0, wr_frames = 0;
        char buf[128];

        snprintf(buf, sizeof(buf), "%d-ckpt.dat", getpid());
        fd = open(buf, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR);

        if (fd < 0) {
                perror("open");
                return -1;
        }

        void    *data   = NULL;
        size_t  size    = 0;

        ckpt_metadata_t meta;
        meta.nr_hdrs    = nr_hdrs;
        meta.nr_rgns    = nr_rgns;
        meta.nr_ctxs    = nr_ctxs;
        meta.nr_frames  = nr_frames;

        data = (void *)&meta;
        size = sizeof(ckpt_metadata_t);
        if (write_data(fd, data, size) < 0) {
                fprintf(stderr, 
                        "[Error] Failed to write checkpoint file "
                        "metadata");
                return -1;
        }
        
        for (int i = 0; i < nr_hdrs; i++) {
                /* Write the checkpoint header itself */
                data = (void *)(hdrs + i);
                size = sizeof(ckpt_hdr_t);
                if (write_data(fd, data, size) < 0) {
                        fprintf(stderr,
                                "[Error] Failed to write "
                                "checkpoint header");
                        return -1;
                }
                wr_hdrs++;

                switch (hdrs[i]) {
                case MEM_RGN_DATA:
                        /**
                         * Write mem_rgn_t data as well as the
                         * actual contents of the memory region
                         */
                        data = (void *)(rgns + wr_rgns);
                        size = sizeof(mem_rgn_t);
                        if (write_data(fd, data, size) < 0)
                                return -1;
                        data = (void *)(rgns[wr_rgns].start);
                        size = (size_t)rgns[wr_rgns].size;
                        if (write_data(fd, data, size) < 0) {
                                fprintf(stderr,
                                        "[Error] Failed to write "
                                        "memory region to "
                                        "checkpoint file\n");
                                return -1;
                        }
                        wr_rgns++;
                        break;
                case REG_CTX_DATA:
                        /**
                         * Write register context data, 
                         * including register values and 
                         * information about PAC signatures
                         */
                        data = (void *)(ctxs + wr_ctxs);
                        size = sizeof(reg_ctx_t);
                        if (write_data(fd, data, size) < 0)
                                return -1;
                        wr_ctxs++;
                        break;
                case CALLFRAME_DATA:
                        /**
                         * Write data for a call frame on the
                         * program stack that contained one or
                         * more PAC signed pointers
                         */
                        data = (void *)(frames + wr_frames);
                        size = sizeof(callframe_t);
                        if (write_data(fd, data, size) < 0)
                                return -1;
                        wr_frames++;
                        break;
                default:
                        assert(0 && "Unreachable\n");
                }
        }
        
        assert((wr_hdrs == nr_hdrs) && (wr_rgns == nr_rgns) && 
               (wr_ctxs == nr_ctxs) && (wr_frames == nr_frames));
        return 0;
}

/**
 * strip_regs:  Strip PAC signatures from saved register context
 * @ctx:        Register context to strip PAC signed pointers
 *              from
 * Note:        This function only handles a PAC-signed link
 *              register for now but should/can be generalized to
 *              handle any signatures
 */
void strip_regs(reg_ctx_t *ctx)
{
        mcontext_t mc = &ctx->mc;

        if (PACSIGNED(mc->__ss.__lr, va_mask)) {
                u64 old         = mc->__ss.__lr;
                u64 expected    = mc->__ss.__lr;
                /**
                 * Strip and re-sign link register to ensure that
                 * the original pacib instruction used to sign
                 * the link register used the stack pointer as a
                 * modifier
                 */
                XPACI(expected);
                PACIB(expected, mc->__ss.__sp);
                assert(expected == old);
                
                /**
                 * Now strip link register, record that the link
                 * register was signed, and that the modifier used
                 * for signing was the stack pointer
                 */
                XPACI(mc->__ss.__lr);
                ctx->modifiers[LR] = SP;
                ctx->pac_bitmap |= (1ULL << LR);
        }
}

void resign_regs(reg_ctx_t *ctx)
{
        mcontext_t mc = &ctx->mc;
        
        if (ctx->pac_bitmap & (1ULL << LR)) {
                if (ctx->modifiers[LR] == SP) {
                        PACIB(mc->__ss.__lr, mc->__ss.__sp);
                } else {
                        assert(0 && "Link register was not signed "
                                    "using the stack pointer as a "
                                    "modifier");
                }
        }
}

/**
 * strip_frames: Walk each frame record on the stack and 
 *               strip PAC signature from any saved link
 *               registers that were previously signed
 *              
 * @frames:      Array of callframe_t structures to record 
 *               information about each frame record encountered
 *               on the stack
 * @fp:          Frame pointer value at the time getcontext() was
 *               originally called for checkpointing
 * @return:      Returns the number of saved call frames, or -1
 *               on error
 */
int strip_frames(callframe_t *frames, u64 *fp)
{
        u64 prev_fp, *prev_lr, prev_sp;
        int nr_frames = 0;
        
        for (;;) {
                prev_fp = fp[0];
                prev_lr = fp + 1;
                prev_sp = (u64)(fp + 2);
                
                /**
                 * If the link register in the current frame was
                 * not signed, mark that is did not contain a
                 * signature and continue to the next frame
                 */
                if (!PACSIGNED(*prev_lr, va_mask)) {
                        if (prev_fp == 0 || prev_fp <= (u64)fp)
                                break;
                        fp = (u64 *)prev_fp;
                        continue;
                }
                
                u64 old         = *prev_lr;
                u64 expected    = *prev_lr;

                /**
                 * Strip and re-sign the link register in the
                 * current call frame, making sure that it was
                 * originally signed with the stack pointer
                 */
                XPACI(expected);
                PACIB(expected, prev_sp);
                assert(expected == old);
                
                /**
                 * Now strip the actual link register and the
                 * information about the current call frame
                 */
                XPACI(*prev_lr);
                frames[nr_frames].fp = prev_fp;
                frames[nr_frames].lr = *prev_lr;
                
                /**
                 * Indicate that the link register was signed
                 * with the IB key and the stack pointer as a
                 * modifier
                 */
                frames[nr_frames].metadata = (SP << 3) |
                                             (PAC_IBKEY << 1) | 1;
                nr_frames++;
                
                if (prev_fp == 0 || prev_fp <= (u64)fp)
                        break;
                fp = (u64 *)prev_fp;
        }

        return nr_frames;
}

void resign_frames(callframe_t *frames, int nr_frames, u64 *fp)
{
        u64 prev_fp, *prev_lr, prev_sp;

        for (int ix = 0; ix < nr_frames; ix++) {
                prev_fp = fp[0];
                while (prev_fp != frames[ix].fp) {
                        fp = (u64 *)prev_fp;
                        if ((u64)fp == 0) {
                                fprintf(stderr,
                                        "Next frame is invalid but "
                                        "there are still frames "
                                        "that need to be re-signed. "
                                        "(%d/%d) frames re-signed "
                                        "before failure. Current "
                                        "frame address: %llu.\n",
                                        ix, nr_frames, (u64)fp);
                                abort();
                        }
                        prev_fp = fp[0];
                }

                prev_lr = fp + 1;
                prev_sp = (u64)(fp + 2);
                
                assert(*prev_lr == frames[ix].lr);
                assert(FR_SIGNED(frames[ix]));
                assert(FR_KEY(frames[ix]) == PAC_IBKEY);
                assert(FR_MOD(frames[ix]) == SP);

                PACIB(*prev_lr, prev_sp);
                
                if (prev_fp == 0 || prev_fp <= (u64)fp) {
                        assert((ix == nr_frames - 1) &&
                               "Next frame is invalid but there "
                               "are still frames that need to be "
                               "re-signed\n");
                        break;
                }
                fp = (u64 *)prev_fp;
        }
}

void ckpt_handler(int sig, siginfo_t *info, void *ucontext)
{
        ucontext_t      uc;
        
        uc              = *(ucontext_t *)ucontext;
        
        /* Make sure ucontext_t was populated with program counter */
        assert(uc.uc_mcontext->__ss.__pc);

        mem_rgn_t       rgns[MAX_MEM_RGNS];
        ckpt_hdr_t      hdrs[MAX_CKPT_HDRS];
        callframe_t     frames[MAX_CALL_FRAMES];
        reg_ctx_t       ctx;
        int             nr_hdrs, nr_rgns, nr_ctxs, nr_frames;
        
        nr_hdrs = 0;
        /**
         * Mark each checkpoint header up to the number of memory
         * regions saved as a header for a memory region
         */
        nr_rgns = get_mem_rgns(rgns);
        for (int i = 0; i < nr_rgns; i++)
                hdrs[i] = MEM_RGN_DATA;
        nr_hdrs += nr_rgns;
        
        /**
         * Mark the next checkpoint header as a register context
         * and store the ucontext_t data to the saved register
         * context
         */
        hdrs[nr_hdrs]           = REG_CTX_DATA;
        ctx.uc                  = uc;
        ctx.mc                  = *uc.uc_mcontext;
        ctx.uc.uc_mcontext      = &ctx.mc;
        ctx.pac_bitmap          = 0;
        nr_ctxs                 = 1;
        nr_hdrs                 += nr_ctxs;
        
        /**
         * Strip PAC signatures from signed registers and store
         * information about which registers were signed
         */
        strip_regs(&ctx);
        
        /**
         * Now walk each frame record on the stack and strip any
         * PAC signed pointers, and save information about modified
         * frame records
         */
        u64 *fp   = (u64 *)ctx.uc.uc_mcontext->__ss.__fp;
        nr_frames = strip_frames(frames, fp);
        nr_hdrs  += nr_frames;
        
        for (int i = nr_rgns + nr_ctxs; i < nr_hdrs; i++)
                hdrs[i] = CALLFRAME_DATA;

        int rc = write_ckpt(nr_hdrs, nr_rgns, nr_ctxs, nr_frames,
                            hdrs, rgns, &ctx, frames);
        
        if (rc < 0) {
                fprintf(stderr,
                        "[Error] Failed to write "
                        "checkpoint file\n");
        }
        
        /**
         * Now re-sign all pointers that were stripped before
         * being written to the checkpoint file to restore the
         * expected signatures when returning to the user program 
         */
        resign_frames(frames, nr_frames, fp);
        printf("Finished writing checkpoint file\n");
}

void __attribute__((constructor)) setup()
{
        sa.sa_sigaction = ckpt_handler;
        sa.sa_flags     = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR2, &sa, NULL);

        size_t size;
        int rc;

        rc = sysctlbyname("machdep.virtual_address_size",
                           &va_bits, &size, NULL, 0);
        if (rc != -1) {
                va_mask = ~((1ULL << va_bits) - 1);
                return;
        }

        rc = sysctlbyname("machdep.cpu.address_bits.virtual",
                           &va_bits, &size, NULL, 0);
        if (rc != -1) {
                va_mask = ~((1ULL << va_bits) - 1);
                return;
        }
        
        fprintf(stderr, "Virtual address size could not be found. "
                        "Assuming a 48 it virtual address space.");
        
        va_mask = ~((1ULL << 48) - 1);
}

void __attribute__((destructor)) cleanup()
{
        return;
}
