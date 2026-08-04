/* macho.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include "common/leb128.h"
#include "common/byte_order.h"
#include "macho.h"
#include <errno.h>
#include <fcntl.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static inline bool fat_is_arm64e(void *);
static inline bool macho_is_arm64e(void *);
static inline bool binary_is_arm64e(void *);
static inline void *fat_arm64e_to_arm64(void *);
static inline void *macho_arm64e_to_arm64(void *);

static inline bool fat_is_arm64e(void *fh_addr)
{
        struct fat_header  *fh;
        struct fat_arch    *fa;
        u32                nfat_arch;
        s32                cputype, subtype;
        bool               bswap;

        fh = (struct fat_header *)fh_addr;
        xnd_assert(!HEADER_IS_64BIT(fh->magic));

        bswap = NEEDS_BSWAP(fh->magic);
	nfat_arch = (bswap ? bswap32(fh->nfat_arch) : fh->nfat_arch);

        fa = (struct fat_arch *)((uchar *)fh_addr + sizeof(*fh));
        for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
		if (bswap) {
			cputype = bswap32(fa->cputype);
			subtype = bswap32(fa->cpusubtype);
		} else {
			cputype = fa->cputype;
			subtype = fa->cpusubtype;
		}

                if (cputype != CPU_TYPE_ARM64)
                        continue;
                else if (CPU_SUBTYPE_IS_ARM64E(subtype))
                        return true;
        }

        return false;
}

static inline bool macho_is_arm64e(void *mh_addr)
{
        struct mach_header_64 *mh;

        mh = (struct mach_header_64 *)mh_addr;
	xnd_assert(HEADER_IS_64BIT(mh->magic));

	if (mh->cputype != CPU_TYPE_ARM64)
		return false;

	return CPU_SUBTYPE_IS_ARM64E(mh->cpusubtype);
}

static inline bool binary_is_arm64e(void *hdr)
{
        u32 magic = *(u32 *)hdr;

        if (HEADER_IS_MACHO(magic))
                return macho_is_arm64e(hdr);
        else if (HEADER_IS_FAT(magic))
                return fat_is_arm64e(hdr);

        return false;
}

static inline void *fat_arm64e_to_arm64(void *addr)
{
        struct fat_header      *fh;
        struct fat_arch        *fa;
        struct mach_header_64  *mh;
        u32                    nfat_arch, offset;
        s32                    cputype;
        bool                   bswap;

        fh = (struct fat_header *)addr;
	xnd_assert(!HEADER_IS_64BIT(fh->magic));

	bswap = NEEDS_BSWAP(fh->magic);
	nfat_arch = (bswap ? bswap32(fh->nfat_arch) : fh->nfat_arch);

	fa = (struct fat_arch *)((char *)addr + sizeof(*fh));
	for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
		cputype = (bswap ? bswap32(fa->cputype) : fa->cputype);
		if (cputype == CPU_TYPE_ARM64)
			break;
	}

	offset = (bswap ? bswap32(fa->offset) : fa->offset);
	mh = (struct mach_header_64 *)((char *)addr + offset);
	xnd_assert(HEADER_IS_MACHO(mh->magic));

        return macho_arm64e_to_arm64((void *)mh);
}

/*
 * macho_arm64e_to_arm64:
 *  Change cpusubtype in mach header of arm64e executable from
 *  CPU_SUBTYPE_ARM64E to CPU_SUBTYPE_ARM64_ALL to create a temporary
 *  copy of the executable that will be loaded as arm64.
 */
static inline void *macho_arm64e_to_arm64(void *addr)
{
        struct mach_header_64 *mh;

        mh = (struct mach_header_64 *)addr;

	xnd_assert(HEADER_IS_64BIT(mh->magic));
	xnd_assert(!NEEDS_BSWAP(mh->magic));
	xnd_assert(mh->cputype == CPU_TYPE_ARM64);

        mh->cpusubtype = CPU_SUBTYPE_ARM64_ALL;
        return (void *)mh;
}

int binary_arm64e_to_arm64(char *path, char *tmp)
{
        int srcfd = -1, dstfd = -1, ret = ARM64E_TO_ARM64_FAILURE;
        void *mh, *addr = NULL;
        size_t size, nbyte;
        off_t off;
        u32 magic;

        if ((srcfd = open(path, O_RDONLY)) < 0) {
                xnd_perror("open");
                goto out;
        }

        if ((off = lseek(srcfd, 0, SEEK_END)) < 0) {
                xnd_perror("lseek");
                goto out;
        }

        size = (size_t)off;
        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, srcfd, 0);
        if (addr == MAP_FAILED) {
                xnd_perror("mmap");
                goto out;
        }

        /* Exit if binary is not an arm64e compiled executable */
        if (!binary_is_arm64e(addr)) {
                ret = ARM64E_TO_ARM64_NOT_ARM64E;
                goto out;
        }

        if ((dstfd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
                xnd_perror("open");
                goto out;
        }

        magic = *(u32 *)addr;
        if (HEADER_IS_MACHO(magic)) {
                mh = macho_arm64e_to_arm64(addr);
        } else if (HEADER_IS_FAT(magic)) {
                mh = fat_arm64e_to_arm64(addr);
        } else {
                xnd_error("Couldn't identify executable: %s\n", path);
                goto out;
        }

        /**
         * In case the input file is a fat binary, subtract the difference
         * of the arm64e mach header and fat header from the total size.
         * The resulting binary should be just one mach-o executable,
         * other architectures can be ignored.
         */
        nbyte = size - (size_t)((char *)mh - (char *)addr);

        /**
         * Hard fail if arm64e executable uses LC_DYLD_INFO(_ONLY)
         * as threaded bind opcodes will be unhandled by dyld if
         * we modify cpu subtype to CPU_SUBTYPE_ARM64_ALL
         */
        if (macho_find_load_command(mh, LC_DYLD_INFO) ||
            macho_find_load_command(mh, LC_DYLD_INFO_ONLY)) {
                xnd_error("Executable uses LC_DYLD_INFO_ONLY: %s\n", path);
                goto out;
        }

        /* Write temporary arm64 executable to disk */
        if (writeall(dstfd, mh, nbyte) == nbyte) {
                ret = ARM64E_TO_ARM64_SUCCESS;
                xnd_trace("Wrote temporary arm64 executable: %s\n", tmp);
        }

out:
        if (srcfd != -1)
                close(srcfd);
        if (dstfd != -1)
                close(dstfd);
        if (addr && addr != MAP_FAILED)
                munmap(addr, size);
        if (ret != ARM64E_TO_ARM64_SUCCESS && access(tmp, F_OK) == 0)
                unlink(tmp);

        return ret;
}

/**
 * macho_find_load_command:
 *  Find specific load command in memory-mapped mach-o executable
 *
 * \mh_addr  Address of executable's mach header
 * \cmd      Load command to search for
 * \return   Pointer to struct load_command if found, otherwise NULL
 */
struct load_command *macho_find_load_command(void *mh_addr, u32 cmd)
{
        struct mach_header *mh;
        struct load_command *lc;
        uintptr_t lc_start;
        bool bswap;
        u32 idx, offset, ncmds;

        mh = (struct mach_header *)mh_addr;
        bswap = NEEDS_BSWAP(mh->magic);
        ncmds = (bswap ? bswap32(mh->ncmds) : mh->ncmds);

        lc_start = (uintptr_t)mh_addr;
        lc_start += (HEADER_IS_64BIT(mh->magic) ?
                     sizeof(struct mach_header_64) :
                     sizeof(struct mach_header));

        for (idx = 0, offset = 0; idx < ncmds; idx++) {
                lc = (struct load_command *)(lc_start + offset);
                if (lc->cmd == cmd)
                        return lc;
                offset += lc->cmdsize;
        }

        return NULL;
}

void *macho_find_segment(void *mh_addr, u32 seg_index)
{
        struct mach_header *mh;
        struct load_command *lc;
        u32 cmdsize, idx = 0;
        uintptr_t lc_start, offset;

        mh = (struct mach_header *)mh_addr;
        lc_start = (uintptr_t)mh;
        lc_start += (HEADER_IS_64BIT(mh->magic) ?
                     sizeof(struct mach_header_64) :
                     sizeof(struct mach_header));

        for (offset = 0; offset < mh->sizeofcmds; offset += cmdsize) {
                lc = (struct load_command *)(lc_start + offset);
                cmdsize = lc->cmdsize;
                if (lc->cmd == LC_SEGMENT || lc->cmd == LC_SEGMENT_64) {
                        if (idx++ == seg_index)
                                return (void *)lc;
                }
        }

        xnd_error("Couldn't find segment at index %u\n", seg_index);
        return NULL;
}

void *macho_thin_from_fat(void *fh_addr, s32 arch)
{
        struct fat_header *fh;
        struct fat_arch *fa;
        u32 nfat_arch, offset;
        s32 cputype;
        bool bswap, found = false;

        fh = (struct fat_header *)fh_addr;
        bswap = NEEDS_BSWAP(fh->magic);
        nfat_arch = (bswap ? bswap32(fh->nfat_arch) : fh->nfat_arch);

        fa = (struct fat_arch *)((char *)fh + sizeof(*fh));
        for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
                cputype = (bswap ? bswap32(fa->cputype) : fa->cputype);
                if (cputype == arch) {
                        found = true;
                        break;
                }
        }

        if (!found) {
                xnd_error("Couldn't find slice with arch %x", arch);
                return NULL;
        }

        offset = (bswap ? bswap32(fa->offset) : fa->offset);
        return (void *)((char *)fh + offset);
}
