#include "../include/ckpt.h"

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

                if (info.is_submap) {
                        depth += 1;
                        continue;
                }

                rgns[nr_rgns].start    = addr;
                rgns[nr_rgns].size     = size;
                rgns[nr_rgns].end      = addr + size;
                rgns[nr_rgns].prot     = info.protection;
                rgns[nr_rgns].max_prot = info.max_protection;

                addr += size;
                nr_rgns++;
        }
        return nr_rgns;
}

int write_data(int fd, void *data, size_t sz)
{
        size_t  bytes = 0;
        ssize_t rc;

        while (bytes < sz) {
                rc = write(fd, data + bytes, sz - bytes);
                if (rc < 0) {
                        perror("write");
                        return -1;
                }
                bytes += rc;
        }

        return bytes == sz;
}

int write_ckpt(ckpt_hdr_t *hdrs, int nr_hdrs)
{
        int  fd, rc;
        char buf[128];

        snprintf(buf, sizeof(buf), "%d-ckpt.dat", getpid());

        if ((fd = open(buf, O_WRONLY, S_IRUSR)) < 0)
                err(EXIT_FAILURE, "open");
        
        for (int i = 0; i < nr_hdrs; i++) {
                rc = write_data(fd,
                                hdrs + i,
                                sizeof(ckpt_hdr_t));
                if (rc < 0)
                        return -1;
        }

        return 0;
}

void ckpt_handler(int sig)
{
        static int is_restart;
        ucontext_t uc;

        is_restart = 0;
        getcontext(&uc);

        if (is_restart)
                return;

        mem_rgn_t       rgns[MAX_MEM_RGNS];
        ckpt_hdr_t      hdrs[MAX_CKPT_HDRS];

        int nr_rgns = get_mem_rgns(rgns);
        for (int i = 0; i < nr_rgns; i++) {
                hdrs[i].rgn  = rgns[i];
                hdrs[i].type = MEM_RGN_HDR;
        }

        hdrs[nr_rgns].uc   = uc;
        hdrs[nr_rgns].type = REG_CTX_HDR;

        int nr_hdrs = nr_rgns + 1;

        if (write_ckpt(hdrs, nr_hdrs) < 0)
                exit(EXIT_FAILURE);
}

void __attribute__((constructor)) setup()
{
        signal(SIGUSR2, ckpt_handler);
}

void __attribute__((destructor)) cleanup()
{
}
