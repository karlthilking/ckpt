/* writeckpt.c */
#include "xnd/xnd.h"
#include "writeckpt.h"
#include "ckptfile.h"
#include "vm_region.h"
#include "util/io.h"
#include "util/env.h"
#include "util/compress.h"

#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

extern u64 epoch;
extern u32 xnd_pid;

int write_vm_page(int fd, struct xnd_vm_region *region,
                  struct xnd_vm_page *page)
{
        ssize_t bytes;
        void    *addr;

	bytes = writeall(fd, page, sizeof(*page));
	if (bytes != sizeof(*page))
		return -1;

        addr = region->start + page->offset;
        bytes = writeall(fd, addr, VM_PAGE_SIZE);
        if (bytes != VM_PAGE_SIZE) {
                xnd_error("Failed to write vm page (%p-%p %s/%s)\n",
                          region->start + page->offset,
                          region->start + page->offset + VM_PAGE_SIZE,
                          VM_PROT_STRING(region->prot),
                          VM_PROT_STRING(region->max_prot));
                return -1;
        }

        return 0;
}

int write_vm_region_pages(int fd, struct xnd_vm_region *region)
{
        kern_return_t           kr;
        mach_vm_address_t       addr;
        mach_vm_size_t          size;
        size_t                  vec_len, idx;
        ssize_t                 bytes;
        uint                    dirty;

        addr = (mach_vm_address_t)region->start;
        size = (mach_vm_size_t)region->size;

        xnd_assert((region->size % VM_PAGE_SIZE) == 0);
        vec_len = region->size / VM_PAGE_SIZE;
        int vec[vec_len];

        kr = mach_vm_page_range_query(mach_task_self(), addr, size,
                                      (mach_vm_address_t)vec,
                                      (mach_vm_size_t *)&vec_len);
        if (kr != KERN_SUCCESS) {
                xnd_error("mach_vm_page_range_query: %s\n",
                          mach_error_string(kr));
                return -1;
        }

        xnd_assert(vec_len == region->size / VM_PAGE_SIZE);
        for (idx = 0, dirty = 0; idx < vec_len; idx++) {
                if (vec[idx] & VM_PAGE_QUERY_PAGE_DIRTY) {
                        dirty++;
                }
        }

        region->pages_dirtied = dirty;
	bytes = writeall(fd, region, sizeof(*region));
	if (bytes != sizeof(*region))
                return -1;

        struct xnd_vm_page pages[dirty], *pg = pages;
        for (idx = 0; idx < vec_len; idx++) {
                if (pg >= pages + dirty) {
                        xnd_assert(idx >= dirty);
                        break;
                }
                if (vec[idx] & VM_PAGE_QUERY_PAGE_DIRTY) {
                        pg->offset = idx * VM_PAGE_SIZE;
                        if (write_vm_page(fd, region, pg) != 0) {
                                return -1;
                        }
                        pg++;
                }
        }

        xnd_assert(pg == pages + dirty);
        return 0;
}

int write_vm_region(int fd, struct xnd_vm_region *region)
{
        ssize_t bytes;

        if (ONLY_SAVE_DIRTY_PAGES(region)) {
                xnd_assert(region->prot & VM_PROT_READ);
                xnd_assert(region->prot != (VM_PROT_READ | VM_PROT_EXECUTE));
                return write_vm_region_pages(fd, region);
        }

	bytes = writeall(fd, region, sizeof(*region));
	if (bytes != sizeof(*region)) {
                xnd_error("Failed to write region info\n");
                return -1;
        }

        /**
         * Write vm region contents. If the region has no protection
         * bits, temporarily grant read permission in order to save
         * the contents of the region to the checkpoint file.
         */
        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, false, VM_PROT_READ) < 0) {
                xnd_error("Failed to get read protect vm region\n"
                          "(%p-%p %zu %s/%s)\n",
                          region->start, region->start + region->size,
                          region->size, VM_PROT_STRING(region->prot),
                          VM_PROT_STRING(region->max_prot));
                return -1;
        }

        bytes = writeall(fd, region->start, region->size);
        if (bytes != region->size) {
               xnd_error("Failed to write vm region contents\n"
                         "(%p-%p %zu %s/%s)\n",
                         region->start, region->start + region->size,
                         region->size, VM_PROT_STRING(region->prot),
                         VM_PROT_STRING(region->max_prot));
                return -1;
        }

        if (region->prot == VM_PROT_NONE &&
            ckpt_vm_protect(region, false, VM_PROT_NONE) < 0) {
                xnd_warn("Failed to restore region protections\n");
        }

        return 0;
}

int write_context(int fd, ucontext_t *uctx)
{
	if (writeall(fd, uctx, sizeof(*uctx)) != sizeof(*uctx))
		return -1;

	return 0;
}

int write_ckpt(struct xnd_ckpt_header *header,
               enum xnd_ckpt_entry *entries,
               struct xnd_vm_region *regions,
               ucontext_t *uctx)
{
        int                     fd = -1, dirfd = -1;
        char                    ckptfile[XND_CKPTFILE_MAXLEN];
        struct xnd_vm_region    *rgn = regions;
        ssize_t                 bytes;
        bool                    use_zlib;

        use_zlib = env_use_zlib_compression();
        xnd_ckptfile_name(ckptfile, sizeof(ckptfile), xnd_pid);
        dirfd = xnd_ckptdir_open(header->xnd_uuid, epoch);
        if (dirfd < 0) {
                xnd_error("Failed to open checkpoint directory\n");
                goto bad;
        }

        fd = xnd_ckptfile_create(dirfd, ckptfile);
        if (fd < 0) {
                xnd_error("Failed to create checkpoint file\n");
                goto bad;
        }

        bytes = writeall(fd, header, sizeof(struct xnd_ckpt_header));
        if (bytes != sizeof(*header)) {
                xnd_error("Failed to write checkpoint header\n");
                goto bad;
        }

        for (u32 i = 0; i < header->entry_count; i++) {
                bytes = writeall(fd, &entries[i], sizeof(entries[i]));
                if (bytes != sizeof(entries[i])) {
                        goto bad;
                }

                switch (entries[i]) {
                case XND_VM_REGION_ENTRY:
			if (write_vm_region(fd, rgn) != 0) {
				xnd_error("Failed to write vm region:\n"
					  "(%p-%p %zu %s/%s)\n",
					  rgn->start,
					  rgn->start + rgn->size,
					  rgn->size,
					  VM_PROT_STRING(rgn->prot),
					  VM_PROT_STRING(rgn->max_prot));
				goto bad;
			}
                        rgn++;
                        break;
                case XND_UCONTEXT_ENTRY:
			if (write_context(fd, uctx) != 0) {
				xnd_error("Failed to write ucontext\n");
				goto bad;
			}
                        break;
                default:
			xnd_error("Unrecognized checkpoint entry\n");
                        xnd_abort();
                }
        }

	if (use_zlib) {
		if (xnd_compress_ckpt(dirfd, ckptfile) != 0)
			goto bad;
	}

        close(dirfd);
        close(fd);
        return 0;
bad:
	if (dirfd != -1)
		close(dirfd);
	if (fd != -1)
		close(fd);
        return -1;
}
