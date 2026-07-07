/* macho.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include "macho.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

static inline bool fat_is_arm64e(void *);
static inline bool macho_is_arm64e(void *);
static inline bool binary_is_arm64e(void *);
static inline void *fat_arm64e_to_arm64(void *);
static inline void *macho_arm64e_to_arm64(void *);

static inline bool fat_is_arm64e(void *addr)
{
        struct fat_header       *fh;
        struct fat_arch         *fa;
        u32                     nfat_arch;
        s32                     cputype, subtype;
        bool                    bswap;

        fh = (struct fat_header *)addr;
        bswap = NEEDS_BSWAP(fh->magic);
        xnd_assert(!HEADER_IS_64BIT(fh->magic));
        
        nfat_arch = fh->nfat_arch;
        if (bswap)
                nfat_arch = __builtin_bswap32(nfat_arch);
        
        fa = (struct fat_arch *)((uchar *)addr + sizeof(*fh));
        for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
                cputype = fa->cputype;
                subtype = fa->cpusubtype;
                if (bswap) {
                        cputype = __builtin_bswap32(cputype);
                        subtype = __builtin_bswap32(subtype);
                }
                if (cputype != CPU_TYPE_ARM64)
                        continue;
                else if (CPU_SUBTYPE_IS_ARM64E(subtype))
                        return true;
        }

        return false;
}

static inline bool macho_is_arm64e(void *addr)
{
        struct mach_header_64   *mh;
        s32                     cputype, subtype;
        
        mh = (struct mach_header_64 *)addr;
        cputype = mh->cputype;
        subtype = mh->cpusubtype;
        
        xnd_assert(HEADER_IS_64BIT(mh->magic));
        if (NEEDS_BSWAP(mh->magic)) {
                cputype = __builtin_bswap32(cputype);
                subtype = __builtin_bswap32(subtype);
        }

        if (cputype != CPU_TYPE_ARM64)
                return false;
        else if (CPU_SUBTYPE_IS_ARM64E(subtype))
                return true;

        return false;
}

static inline bool binary_is_arm64e(void *addr)
{
        u32 magic = *(u32 *)addr;

        if (HEADER_IS_MACHO(magic))
                return macho_is_arm64e(addr);
        else if (HEADER_IS_FAT(magic))
                return fat_is_arm64e(addr);
        
        return false;
}

static inline void *fat_arm64e_to_arm64(void *addr)
{
        struct fat_header       *fh;
        struct fat_arch         *fa;
        struct mach_header_64   *mh;
        u32                     nfat_arch, offset;
        s32                     cputype, subtype;
        bool                    bswap;

        fh = (struct fat_header *)addr;
        bswap = NEEDS_BSWAP(fh->magic);
        xnd_assert(!HEADER_IS_64BIT(fh->magic));
        
        nfat_arch = fh->nfat_arch;
        subtype = CPU_SUBTYPE_ARM64_ALL;
        if (bswap) {
                nfat_arch = __builtin_bswap32(nfat_arch);
                subtype = __builtin_bswap32(subtype);
        }

        fa = (struct fat_arch *)((uchar *)addr + sizeof(*fh));
        for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
                cputype = fa->cputype;
                if (bswap)
                        cputype = __builtin_bswap32(cputype);
                if (cputype == CPU_TYPE_ARM64)
                        break;
        }
        
        offset = (bswap ? __builtin_bswap32(fa->offset) : fa->offset);
        mh = (struct mach_header_64 *)((uchar *)addr + offset);
        
        xnd_assert(HEADER_IS_MACHO(mh->magic));
        return macho_arm64e_to_arm64((void *)mh);
}

static inline void *macho_arm64e_to_arm64(void *addr)
{
        struct mach_header_64   *mh;
        s32                     cputype, subtype;
        
        mh = (struct mach_header_64 *)addr;
        cputype = mh->cputype;
        subtype = CPU_SUBTYPE_ARM64_ALL;
        
        xnd_assert(HEADER_IS_64BIT(mh->magic));
        if (NEEDS_BSWAP(mh->magic)) {
                cputype = __builtin_bswap32(cputype);
                subtype = __builtin_bswap32(subtype);
        }

        xnd_assert(cputype == CPU_TYPE_ARM64);
        mh->cpusubtype = subtype;

        return (void *)mh;
}

bool binary_arm64e_to_arm64(char *path, char *tmp)
{
        int     srcfd = -1, dstfd = -1;
        void    *start, *addr = NULL;
        size_t  size, nbyte;
        off_t   off;
        u32     magic;
        bool    ret = false;

        if ((srcfd = open(path, O_RDONLY)) < 0) {
                xnd_error("open: %s\n", strerror(errno));
                goto out;
        }
        
        if ((off = lseek(srcfd, 0, SEEK_END)) < 0) {
                xnd_error("lseek: %s\n", strerror(errno));
                goto out;
        } else {
                size = (size_t)off;
        }

        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, srcfd, 0);
        if (addr == MAP_FAILED) {
                xnd_error("mmap: %s\n", strerror(errno));
                goto out;
        }

        if (!binary_is_arm64e(addr))
                goto out;

        if ((dstfd = open(tmp, O_WRONLY | O_CREAT, 0755)) < 0) {
                xnd_error("open: %s\n", strerror(errno));
                goto out;
        }
        
        magic = *(u32 *)addr;
        if (HEADER_IS_MACHO(magic)) {
                ret = true;
                start = macho_arm64e_to_arm64(addr);
        } else if (HEADER_IS_FAT(magic)) {
                ret = true;
                start = fat_arm64e_to_arm64(addr);
        } else {
                goto out;
        }
        
        nbyte = size - (start - addr);
        if (writeall(dstfd, start, nbyte) != nbyte) {
                xnd_error("Failed to make temporary arm64 binary: %s\n", tmp);
                xnd_abort();
        }
out:
        if (srcfd != -1)
                close(srcfd);
        if (dstfd != -1)
                close(dstfd);
        if (addr && addr != MAP_FAILED)
                munmap(addr, size);
        if (ret == false && access(tmp, F_OK) == 0)
                unlink(tmp);

        return ret;
}
