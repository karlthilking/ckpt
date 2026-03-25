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

/* Globals */
__attribute__((visibility("hidden"))) u32 vabits;
__attribute__((visibility("hidden"))) u64 vamask;


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
         * A page with no permissions is likely a guard page
         * or zero page, do not checkpoint
         */
        if ((info->protection == VM_PROT_NONE) ||
            !(info->protection & VM_PROT_READ))
                return 0;
        
        /**
         * If the region address falls within the dyld shared
         * cache, it should not be checkpointed
         */
        if (addr >= SHARED_REGION_BASE_ARM64 &&
            addr < SHARED_REGION_BASE_ARM64 +
                   SHARED_REGION_SIZE_ARM64) {
                return 0;
        }
        
        /**
         * Do not checkpoint guard pages and shared pmap
         */
        switch (info->user_tag) {
        case VM_MEMORY_GUARD:
        case VM_MEMORY_SHARED_PMAP:
                return 0;
        default:
                break;
        }

        /**
         * Skip checkpointing shared regions that this process
         * does have write permissions to
         */
        if ((info->share_mode & SM_SHARED) &&
            !(info->protection & VM_PROT_WRITE))
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

                if (kr == KERN_INVALID_ADDRESS)
                         break;
                else if (kr != KERN_SUCCESS) {
                        fprintf(stderr,
                                "mach_vm_region_recurse: %s\n",
                                mach_error_string(kr));
                        break;
                }
                
                /**
                 * Skip over memory regions which should not
                 * be checkpointed
                 */
                if (!mem_rgn_valid(&info, addr, size)) {
                        addr += size;
                        continue;
                } else if (info.is_submap) {
                        depth += 1;
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
        size_t  bytes = 0;
        ssize_t rc;

        while (bytes < size) {
                rc = write(fd, data + bytes, size - bytes);
                if (rc < 0) {
                        perror("write");
                        return -1;
                }
                bytes += rc;
        }

        return (bytes == size) ? 0 : -1;
}

/**
 * write_ckpt:  Write the checkpoint file to disk
 *
 * @hdrs:       The checkpoint headers to write to the file
 * @nr_hdrs:    The number of checkpoint headers to write
 * return:      Returns -1 on failure, 0 on success
 */
int write_ckpt(ckpt_hdr_t *hdrs, int nr_hdrs)
{
        int  fd, rc;
        char buf[128];

        snprintf(buf, sizeof(buf), "%d-ckpt.dat", getpid());

        if ((fd = open(buf, O_CREAT | O_WRONLY, S_IRUSR)) < 0) {
                perror("open");
                return -1;
        }
        
        for (int i = 0; i < nr_hdrs; i++) {
                /* Write the checkpoint header */
                rc = write_data(fd,
                                hdrs + i,
                                sizeof(ckpt_hdr_t));
                if (rc < 0)
                        return -1;
                
                if (hdrs[i].type & REG_CTX_HDR)
                        continue;
        
                /**
                 * If this header is for a memory region,
                 * write the contents of that memory region
                 */
                assert(hdrs[i].rgn.prot & VM_PROT_READ);
                rc = write_data(fd,
                                (void *)hdrs[i].rgn.start,
                                hdrs[i].rgn.size);
                if (rc < 0)
                        return -1;
        }

        return 0;
}

/**
 * pac_strip:   Remove the PAC signature from each pointer in
 *              a ucontext_t structure, using the virtual
 *              address space size to determine if the pointer
 *              is signed, i.e. if (ptr & addr_high_bits) ->
 *              the pointer is signed.
 *
 * @ctx:        The saved register context to stripped pointer
 *              authentication signatures from
 */
void pac_strip(reg_ctx_t *ctx)
{
        mcontext_t mc = ctx->uc.uc_mcontext;
        
        if (PACSIGNED(mc->__ss.__lr, vamask)) {
                XPACI(mc->__ss.__lr);
                ctx->stripped = 1;
        }
}

/**
 * pac_resign:  Re-sign pointers that were previously signed
 *              before stripping the PAC at checkpoint time to
 *              ensure authentication does not fail after
 *              returning.
 *
 * @ctx:        The restored register context potentially
 *              containing register that should be re-signed
 */
void pac_resign(reg_ctx_t *ctx)
{
        mcontext_t mc = ctx->uc.uc_mcontext;

        if (!ctx->stripped)
                return;

        PACIB(mc->__ss.__lr, mc->__ss.__sp);
        assert(PACSIGNED(mc->__ss.__lr, vamask));
}

void ckpt_handler(int sig)
{
        static int is_restart;
        ucontext_t uc;

        is_restart = 0;
        getcontext(&uc);

        if (is_restart)
                return;
        
        is_restart = 1;

        mem_rgn_t       rgns[MAX_MEM_RGNS];
        ckpt_hdr_t      hdrs[MAX_CKPT_HDRS];

        int nr_rgns = get_mem_rgns(rgns);
        for (int i = 0; i < nr_rgns; i++) {
                hdrs[i].rgn  = rgns[i];
                hdrs[i].type = MEM_RGN_HDR;
        }
        
        hdrs[nr_rgns].ctx.uc    = uc;
        hdrs[nr_rgns].type      = REG_CTX_HDR;

        /**
         * Strip any signed pointers saved in the register
         * context
         */
        pac_strip(&hdrs[nr_rgns].ctx);

        int nr_hdrs = nr_rgns + 1;
        if (write_ckpt(hdrs, nr_hdrs) < 0)
                fprintf(stderr, "Failed to write checkpoint\n");
}

void __attribute__((constructor)) setup()
{
        signal(SIGUSR2, ckpt_handler);
        size_t size;
        int rc;

        rc = sysctlbyname("machdep.virtual_address_size",
                           &vabits, &size, NULL, 0);
        
        if (rc != -1) {
                vamask = ~((UINT64_C(1) << vabits) - 1);
                return;
        }

        rc = sysctlbyname("machdep.cpu.address_bits.virtual",
                           &vabits, &size, NULL, 0);
        
        if (rc != -1) {
                vamask = ~((UINT64_C(1) << vabits) - 1);
                return;
        }
        
        fprintf(stderr, "The virtual address space size could "
                        "not be determined: %s\n. Assuming "
                        "a 48 bit virtual address space",
                        strerror(errno));
        
        vamask = ~((UINT64_C(1) << vabits) - 1);
}

void __attribute__((destructor)) cleanup()
{
        return;
}
