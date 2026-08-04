/* macho.h */
#ifndef XND_MACHO_H
#define XND_MACHO_H

#include "xnd/xnd.h"
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <mach-o/dyld.h>
#include <mach-o/fixup-chains.h>
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

#define BIND_TYPE_REGULAR  0x1
#define BIND_TYPE_WEAK     0x2
#define BIND_TYPE_LAZY     0x4
#define BIND_TYPE_ALL \
        (BIND_TYPE_REGULAR | BIND_TYPE_WEAK | BIND_TYPE_LAZY)

struct macho_info {
        struct mach_header        *mh;
        struct dyld_info_command  *cmd;
};

#define ARM64E_TO_ARM64_SUCCESS      0
#define ARM64E_TO_ARM64_FAILURE     -1
#define ARM64E_TO_ARM64_NOT_ARM64E  -2

int binary_arm64e_to_arm64(char *, char *);
struct load_command *macho_find_load_command(void *, u32);
void *macho_find_segment(void *, u32);
void *macho_thin_from_fat(void *, s32);

#endif /* XND_MACHO_H */
