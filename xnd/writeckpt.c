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

static inline int write_vm_page(int fd, struct xnd_vm_region *region,
                                struct xnd_vm_page *page)
{
	void *addr;
	ssize_t bytes;

	bytes = writeall(fd, page, sizeof(*page));
	if (bytes != sizeof(*page))
		return -1;

	addr = region->start + page->offset;
	bytes = writeall(fd, addr, VM_PAGE_SIZE);
	if (bytes != VM_PAGE_SIZE) {
		xnd_error("Failed to write vm page: %s\n",
			  vm_page_string(region, page));
		return -1;
	}

        return 0;
}

static int write_vm_region_dirty(int fd, struct xnd_vm_region *region)
{
	kern_return_t kr;
	uint dirty;
	ssize_t bytes;
	size_t vec_len, idx;
	mach_vm_size_t size;
	mach_vm_address_t addr;

        addr = (mach_vm_address_t)region->start;
        size = (mach_vm_size_t)region->size;

	if (region->prot == (VM_PROT_READ | VM_PROT_EXECUTE)) {
		xnd_warn("Dirty page region is executable: %s\n",
			 vm_region_string(region));
	}

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
		if ((vec[idx] & VM_PAGE_QUERY_PAGE_DIRTY) != 0)
			dirty++;
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
		if ((vec[idx] & VM_PAGE_QUERY_PAGE_DIRTY) != 0) {
			pg->offset = idx * VM_PAGE_SIZE;
			if (write_vm_page(fd, region, pg) != 0)
				return -1;
			pg++;
		}
        }

        xnd_assert(pg == pages + dirty);
        return 0;
}

/*
 * write_vm_region_all_pages:
 *  Attempt to write region contents one page at a time.
 */
static int write_vm_region_all_pages(int fd, struct xnd_vm_region *region)
{
	int p;
	off_t ret, seek;
	ssize_t bytes;

	seek = lseek(fd, 0, SEEK_CUR);
	xnd_assert(seek != -1);

	for (p = 0; p < region->size / VM_PAGE_SIZE; p++) {
		char *addr = (char *)region->start + p * VM_PAGE_SIZE;
		bytes = writeall(fd, addr, VM_PAGE_SIZE);
		if (bytes != VM_PAGE_SIZE) {
			/*
			 * If the failed write was a partial write, seek
			 * backwards to the old file offset so the partial
			 * write can be ignored.
			 */
			if (bytes != 0) {
				ret = lseek(fd, seek - bytes, SEEK_SET);
				xnd_assert(ret != -1);
			}
			break;
		}
		seek += VM_PAGE_SIZE;
	}

	return p;
}

int write_vm_region(int fd, struct xnd_vm_region *region)
{
	off_t before, after;
	int pages;
	ssize_t bytes;
	bool read_protect;

	/*
	 * Only save dirty pages for shared cache regions, malloc
	 * arenas, etc.
	 */
	if (ONLY_SAVE_DIRTY_PAGES(region))
		return write_vm_region_dirty(fd, region);

	/*
	 * Save current position to manipulate region fields later
	 * on if necessary
	 */
	before = lseek(fd, 0, SEEK_CUR);
	xnd_assert(before != -1);

	bytes = writeall(fd, region, sizeof(*region));
	if (bytes != sizeof(*region)) {
		xnd_error("Failed to write region info\n");
		return -1;
	}

	/*
	 * Obtain read permissions for region if not currently readable.
	 */
	read_protect = ((region->prot & VM_PROT_READ) == 0);
	if (read_protect) {
		vm_prot_t prot = region->prot | VM_PROT_READ;
		if (ckpt_vm_protect(region, false, prot) != 0) {
			xnd_error("Failed to read protect region: %s\n",
				  vm_region_string(region));
			return -1;
		}
	}

	pages = write_vm_region_all_pages(fd, region);
	if (pages == region->size / VM_PAGE_SIZE)
		goto out;

	/*
	 * If only some pages could be written successfully, correct
	 * the region's size to reflect how many bytes could be written.
	 *
	 * Restore old file offset and rewrite region structure to
	 * reflect the dynamic adjustment for failed/partial page writes.
	 */
	after = lseek(fd, 0, SEEK_CUR);
	xnd_assert(after != -1);
	if (lseek(fd, before, SEEK_SET) < 0) {
		xnd_perror("lseek");
		return -1;
	}

	region->size = pages * VM_PAGE_SIZE;
	bytes = writeall(fd, region, sizeof(*region));
	if (bytes != sizeof(*region)) {
		xnd_error("Failed to update region size: %s\n",
			  vm_region_string(region));
		return -1;
	}

	if (lseek(fd, after, SEEK_SET) < 0) {
		xnd_perror("lseek");
		return -1;
	}

out:
        if (read_protect) {
		if (ckpt_vm_protect(region, false, region->prot) != 0) {
			xnd_warn("Failed to restore vm protection: %s\n",
				 vm_region_string(region));
		}
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
	int fd = -1, dirfd = -1;
	ssize_t bytes;
	bool use_zlib;
	char ckptfile[XND_CKPTFILE_MAXLEN];
	struct xnd_vm_region *region = regions;

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

	bytes = writeall(fd, header, sizeof(*header));
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
			if (write_vm_region(fd, region) != 0) {
				xnd_error("Failed to write region: %s\n",
					  vm_region_string(region));
				goto bad;
			}
                        region++;
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
