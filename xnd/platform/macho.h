/* macho.h */
#ifndef XND_MACHO_H
#define XND_MACHO_H

#include "xnd/xnd.h"
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/dyld.h>
#include <mach/machine.h>
#include <machine/endian.h>

#define HEADER_IS_FAT(magic) \
        ((magic) == FAT_MAGIC || (magic) == FAT_CIGAM || \
         (magic) == FAT_MAGIC_64 || (magic) == FAT_CIGAM_64)
#define HEADER_IS_MACHO(magic) \
        ((magic) == MH_MAGIC || (magic) == MH_CIGAM || \
         (magic) == MH_MAGIC_64 || (magic) == MH_CIGAM_64)
#define HEADER_IS_64BIT(magic) \
        ((magic) == FAT_MAGIC_64 || (magic) == FAT_CIGAM_64 || \
         (magic) == MH_MAGIC_64 || (magic) == MH_CIGAM_64)
#define NEEDS_BSWAP(magic) \
        ((magic) == MH_CIGAM || (magic) == MH_CIGAM_64 || \
         (magic) == FAT_CIGAM || (magic) == FAT_CIGAM_64)

#define CPU_SUBTYPE_IS_ARM64E(subtype) \
        (((subtype) & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E)

bool binary_arm64e_to_arm64(char *, char *);

#endif /* XND_MACHO_H */
