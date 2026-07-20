/* macho.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include "common/leb128.h"
#include "macho.h"
#include <errno.h>
#include <fcntl.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static inline void lc_shift_offset32(struct macho_info *, u32 *);
static inline void lc_shift_offset64(struct macho_info *, u64 *);

static inline void macho_shift_dyld_info(struct macho_info *);
static inline void macho_shift_data_in_code(struct macho_info *, void *);
static inline void macho_shift_segment(struct macho_info *, void *);
static int macho_shift_offsets(struct macho_info *);

static inline u32 macho_read_threaded_subopcode(u8 **, u8);
static u32 macho_sizeof_arm64e_opcodes(struct macho_info *, int);

static struct load_command *macho_find_load_command(void *, u32);
static inline bool fat_is_arm64e(void *);
static inline bool macho_is_arm64e(void *);
static inline bool binary_is_arm64e(void *);
static inline void *fat_arm64e_to_arm64(void *);
static inline void *macho_arm64e_to_arm64(void *);

static inline u32 macho_section_end(struct macho_info *, u32, bool);
static inline size_t macho_sizeof_patched_bind_info(struct macho_info *, u32);
static u8 *macho_serialize_opcodes(struct macho_info *, u32, u32);
static int macho_write_patched_executable(struct macho_info *, int, size_t);
static int arm64e_with_dyld_info_to_arm64(int, void *, size_t,
					  struct dyld_info_command *);

static inline void lc_shift_offset32(struct macho_info *info, u32 *offset)
{
	struct dyld_info_command  *cmd;
	u32                       shift, old;

	if ((old = *offset) != 0) {
		shift = 0;
		cmd = info->cmd;
		if (old >= cmd->bind_off + cmd->bind_size)
			shift += info->bind_shift;
		if (old >= cmd->weak_bind_off + cmd->weak_bind_size)
			shift += info->weak_bind_shift;
		if (old >= cmd->lazy_bind_off + cmd->lazy_bind_size)
			shift += info->lazy_bind_shift;
		*offset = old - shift;
	}
}

static inline void lc_shift_offset64(struct macho_info *info, u64 *offset)
{
	struct dyld_info_command  *cmd;
	u64                       shift, old;

	if ((old = *offset) != 0) {
		shift = 0;
		cmd = info->cmd;
		if (old >= cmd->bind_off + cmd->bind_size)
			shift += info->bind_shift;
		if (old >= cmd->weak_bind_off + cmd->weak_bind_size)
			shift += info->weak_bind_shift;
		if (old >= cmd->lazy_bind_off + cmd->lazy_bind_size)
			shift += info->lazy_bind_shift;
		*offset = old - shift;
	}
}

static inline void macho_shift_dyld_info(struct macho_info *info)
{
	struct dyld_info_command  *cmd = info->cmd;
	u32                       bind_off, weak_bind_off, lazy_bind_off;

	/**
	 * rebase_off and export_off can be shifted independently without
	 * any side effects
	 */
	lc_shift_offset32(info, &cmd->rebase_off);
	lc_shift_offset32(info, &cmd->export_off);

	/* Apply bind offset shifts on temporary variables */
	bind_off = cmd->bind_off;
	weak_bind_off = cmd->weak_bind_off;
	lazy_bind_off = cmd->lazy_bind_off;

	lc_shift_offset32(info, &bind_off);
	lc_shift_offset32(info, &weak_bind_off);
	lc_shift_offset32(info, &lazy_bind_off);

	/**
	 * Now apply shifts to bind offsets and subtract shift
	 * from bind section sizes
	 */
	cmd->bind_off = bind_off;
	cmd->bind_size -= info->bind_shift;

	cmd->weak_bind_off = weak_bind_off;
	cmd->weak_bind_size -= info->weak_bind_shift;

	cmd->lazy_bind_off = lazy_bind_off;
	cmd->lazy_bind_size -= info->lazy_bind_shift;
}

static inline void macho_shift_data_in_code(struct macho_info *info, void *lc)
{
	struct linkedit_data_command  *cmd;
	struct data_in_code_entry     *entry;
	uintptr_t                     vm_off;
	u32                           idx, step;

	step = sizeof(struct data_in_code_entry);
	cmd = (struct linkedit_data_command *)lc;
	xnd_assert(cmd->cmd == LC_DATA_IN_CODE);

	vm_off = (uintptr_t)info->mh + cmd->dataoff;
	entry = (struct data_in_code_entry *)vm_off;
	
	for (idx = 0; idx < cmd->datasize / step; idx++, entry++)
		lc_shift_offset32(info, &entry->offset);

	lc_shift_offset32(info, &cmd->dataoff);
}

static inline void macho_shift_segment(struct macho_info *info, void *lc)
{
	u32 cmd = ((struct load_command *)lc)->cmd;

	switch (cmd) {
	case LC_SEGMENT: {
		struct segment_command *seg;
		struct section         *sect;

		seg = (struct segment_command *)lc;
		lc_shift_offset32(info, &seg->fileoff);

		/**
		 * If this is a load command for the __LINKEDIT segment,
		 * subtract number of omitted arm64e opcodes from the
		 * segment's file size
		 */
		if (strcmp(seg->segname, SEG_LINKEDIT) == 0) {
			seg->filesize -= (info->bind_shift +
					  info->weak_bind_shift +
					  info->lazy_bind_shift);
		}

		sect = (struct section *)((char *)seg + sizeof(*seg));
		for (u32 idx = 0; idx < seg->nsects; idx++, sect++) {
			lc_shift_offset32(info, &sect->offset);
			lc_shift_offset32(info, &sect->reloff);
		}
		break;
	}
	case LC_SEGMENT_64: {
		struct segment_command_64 *seg;
		struct section_64         *sect;

		seg = (struct segment_command_64 *)lc;
		lc_shift_offset64(info, &seg->fileoff);

		if (strcmp(seg->segname, SEG_LINKEDIT) == 0) {
			seg->filesize -= (info->bind_shift +
					  info->weak_bind_shift +
					  info->lazy_bind_shift);
		}

		sect = (struct section_64 *)((char *)seg + sizeof(*seg));
		for (u32 idx = 0; idx < seg->nsects; idx++, sect++) {
			lc_shift_offset32(info, &sect->offset);
			lc_shift_offset32(info, &sect->reloff);
		}
		break;
	}
	default: 
		xnd_error("Expected LC_SEGMENT or LC_SEGMENT_64\n");
		xnd_abort();
	}
}

/**
 * macho_shift_offsets:
 *   Shift all relative offsets encoded in mach-o executable to account
 *   for bind opcodes that will be stripped.
 *
 * \info  Pointer to struct macho_info containg bind and shift info
 */
static int macho_shift_offsets(struct macho_info *info)
{
	struct mach_header   *mh;
	struct load_command  *lc;
	uintptr_t            start;
	u32                  idx, cmdsize, offset = 0;

	mh = (struct mach_header *)info->mh;
	xnd_assert(!NEEDS_BSWAP(mh->magic));

	start = (uintptr_t)mh;
	if (HEADER_IS_64BIT(mh->magic))
		start += sizeof(struct mach_header_64);
	else
		start += sizeof(struct mach_header);

	for (idx = 0; idx < mh->ncmds; offset += cmdsize, idx++) {
		lc = (struct load_command *)(start + offset);
		cmdsize = lc->cmdsize;
		switch (lc->cmd) {
		case LC_SEGMENT:
		case LC_SEGMENT_64: {
			macho_shift_segment(info, lc);
			break;
		}
		case LC_SYMTAB: {
			struct symtab_command *cmd =
				(struct symtab_command *)lc;
			lc_shift_offset32(info, &cmd->symoff);
			lc_shift_offset32(info, &cmd->stroff);
			break;
		}
		case LC_DYSYMTAB: {
			struct dysymtab_command *cmd =
				(struct dysymtab_command *)lc;
			lc_shift_offset32(info, &cmd->tocoff);
			lc_shift_offset32(info, &cmd->modtaboff);
			lc_shift_offset32(info, &cmd->extrefsymoff);
			lc_shift_offset32(info, &cmd->indirectsymoff);
			lc_shift_offset32(info, &cmd->extreloff);
			lc_shift_offset32(info, &cmd->locreloff);
			break;
		}
		case LC_TWOLEVEL_HINTS: {
			struct twolevel_hints_command *cmd =
				(struct twolevel_hints_command *)lc;
			lc_shift_offset32(info, &cmd->offset);
			break;
		}
		case LC_DATA_IN_CODE: {
			macho_shift_data_in_code(info, lc);
			break;
		}
		case LC_CODE_SIGNATURE:
		case LC_SEGMENT_SPLIT_INFO:
		case LC_FUNCTION_STARTS:
		case LC_DYLIB_CODE_SIGN_DRS:
		case LC_LINKER_OPTIMIZATION_HINT: {
			struct linkedit_data_command *cmd =
				(struct linkedit_data_command *)lc;
			lc_shift_offset32(info, &cmd->dataoff);
			break;
		}
		case LC_FILESET_ENTRY: {
			struct fileset_entry_command *cmd =
				(struct fileset_entry_command *)lc;
			lc_shift_offset64(info, &cmd->fileoff);
			break;
		}
		case LC_ENCRYPTION_INFO:
		case LC_ENCRYPTION_INFO_64: {
			struct encryption_info_command *cmd =
				(struct encryption_info_command *)lc;
			lc_shift_offset32(info, &cmd->cryptoff);
			break;
		}
	        case LC_DYLD_INFO:
		case LC_DYLD_INFO_ONLY: {
			/**
			 * Don't manipulate dyld info load command for now,
			 * sizes and offsets can be shifted later.
			 */
			xnd_assert((uintptr_t)lc == (uintptr_t)info->cmd);
			break;
		}
		case LC_SYMSEG: {
			struct symseg_command *cmd =
				(struct symseg_command *)lc;
			lc_shift_offset32(info, &cmd->offset);
			break;
		}
		case LC_MAIN: {
			struct entry_point_command *cmd =
				(struct entry_point_command *)lc;
			lc_shift_offset64(info, &cmd->entryoff);
			break;
		}
		case LC_NOTE: {
			struct note_command *cmd =
				(struct note_command *)lc;
			lc_shift_offset64(info, &cmd->offset);
			break;
		}
		case LC_DYLD_EXPORTS_TRIE:
			xnd_error("Unexpected LC_DYLD_EXPORTS_TRIE\n");
			return -1;
		case LC_DYLD_CHAINED_FIXUPS:
			xnd_error("Unexpected LC_DYLD_CHAINED_FIXUPS\n");
			return -1;
		default:
			break;
		}
	}

	return 0;
}

static inline u32 macho_read_threaded_subopcode(u8 **p, u8 imm)
{
	u8   *next, *itr = *p;
	u32  ret = 0;

	switch (imm) {
	case BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB:
		next = decode_uleb128(itr, NULL);
		*p = next;
		ret = (u32)(next - itr);
		break;
	case BIND_SUBOPCODE_THREADED_APPLY:
		break;
	default:
		xnd_warn("Unknown threaded bind subopcode: 0x%x\n", imm);
	}

	return ret;
}

/**
 * macho_sizeof_arm64e_opcodes:
 *  Total number in bytes of arm64e bind opcodes in mach-o executable
 *
 * \info    struct containing mach-o executable info
 * \which   which bind info type to look for
 * \return  size in bytes of arm64e bind opcodes
 */
static u32 macho_sizeof_arm64e_opcodes(struct macho_info *info, int which)
{
	u8   *itr, *op_start, *op_end, imm, op;
	u32  bind_off, bind_size, bytes = 0;

	switch (which) {
	case BIND_TYPE_REGULAR:
		bind_off = info->cmd->bind_off;
		bind_size = info->cmd->bind_size;
		break;
	case BIND_TYPE_WEAK:
		bind_off = info->cmd->weak_bind_off;
		bind_size = info->cmd->weak_bind_size;
		break;
	case BIND_TYPE_LAZY:
		bind_off = info->cmd->lazy_bind_off;
		bind_size = info->cmd->lazy_bind_size;
		break;
	default:
		xnd_error("Unknown bind identifier: 0x%x\n", which);
		xnd_abort();
		unreachable();
	}

	op_start = (u8 *)info->mh + bind_off;
	op_end = op_start + bind_size;

	itr = op_start;
	while (itr != op_end) {
		/* Extract bind opcode and immediate argument */
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		itr++;
		switch (op) {
		case BIND_OPCODE_DONE:
			goto done;
		case BIND_OPCODE_DO_BIND:
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
		case BIND_OPCODE_SET_TYPE_IMM:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
			/* No extra data */
			break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			/* Consume symbol name string */
			while (*itr != '\0')
				itr++;
			itr++;
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			/* Consume one sleb128 encoded number */
			itr = decode_sleb128(itr, NULL);
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
		case BIND_OPCODE_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			/**
			 * Consume one uleb128 encoded number and
			 * advance iterator
			 */
			itr = decode_uleb128(itr, NULL);
			break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			/* Consume two uleb128 encoded numbers */
			itr = decode_uleb128(itr, NULL);
			itr = decode_uleb128(itr, NULL);
			break;
		case BIND_OPCODE_THREADED:
			/* At least one threaded opcode/byte */
			bytes++;
			/**
			 * If there's more data (uleb128 encoded), consume
			 * extra data and add to number of opcode bytes
			 */
			bytes += macho_read_threaded_subopcode(&itr, imm);
			break;
		default:
			xnd_warn("Unknown bind opcode: 0x%x\n", op | imm);
			break;
		}
	}

done:
	switch (which) {
	case BIND_TYPE_REGULAR:
		info->bind_shift = bytes;
		break;
	case BIND_TYPE_WEAK:
		info->weak_bind_shift = bytes;
		break;
	case BIND_TYPE_LAZY:
		info->lazy_bind_shift = bytes;
		break;
	}

	return bytes;
}

/**
 * macho_find_load_command:
 *  Find specific load command in memory-mapped mach-o executable
 *
 * \mh_addr  Address of executable's mach header
 * \cmd      Load command to search for
 * \return   Pointer to struct load_command if found, otherwise NULL
 */
static struct load_command *macho_find_load_command(void *mh_addr, u32 cmd)
{
        struct mach_header      *mh;
        struct load_command     *lc;
	uintptr_t               lc_start;
        u32                     idx, offset, align, ncmds, sizeofcmds;

        mh = (struct mach_header *)mh_addr;
        ncmds = mh->ncmds;
        sizeofcmds = mh->sizeofcmds;
        if (NEEDS_BSWAP(mh->magic)) {
                ncmds = __builtin_bswap32(ncmds);
                sizeofcmds = __builtin_bswap32(sizeofcmds);
        }
        
        lc_start = (uintptr_t)mh_addr;
        if (HEADER_IS_64BIT(mh->magic)) {
                align = 8u;
                lc_start += sizeof(struct mach_header_64);
        } else {
                align = 4u;
                lc_start += sizeof(struct mach_header);
        }

	offset = 0;
	for (idx = 0; idx < ncmds; idx++) {
		lc = (struct load_command *)(lc_start + offset);
                xnd_assert((lc->cmdsize % align) == 0);
		if (lc->cmd == cmd)
			return lc;
                offset += lc->cmdsize;
        }

	return NULL;
}

static inline bool fat_is_arm64e(void *fh_addr)
{
        struct fat_header       *fh;
        struct fat_arch         *fa;
        u32                     nfat_arch;
        s32                     cputype, subtype;
        bool                    bswap;

        fh = (struct fat_header *)fh_addr;
        bswap = NEEDS_BSWAP(fh->magic);
        xnd_assert(!HEADER_IS_64BIT(fh->magic));
        
        nfat_arch = fh->nfat_arch;
        if (bswap)
                nfat_arch = __builtin_bswap32(nfat_arch);
        
        fa = (struct fat_arch *)((uchar *)fh_addr + sizeof(*fh));
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

static inline bool macho_is_arm64e(void *mh_addr)
{
        struct mach_header_64   *mh;
        s32                     cputype, subtype;
        
        mh = (struct mach_header_64 *)mh_addr;
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

static inline u32 macho_section_end(struct macho_info *info,
				    u32 loff, bool has_opcodes)
{
	struct dyld_info_command  *old = info->old_cmd;
	u32                       roff = UINT32_MAX;

	if (has_opcodes) {
		if (loff == old->bind_off)
			roff = old->bind_off + old->bind_size;
		else if (loff == old->weak_bind_off)
			roff = old->weak_bind_off + old->weak_bind_size;
		else if (loff == old->lazy_bind_off)
			roff = old->lazy_bind_off + old->lazy_bind_size;
	} else {
		if (old->bind_off && old->bind_off >= loff)
			roff = min(roff, old->bind_off);
		else if (old->weak_bind_off && old->weak_bind_off >= loff)
			roff = min(roff, old->weak_bind_off);
		else if (old->lazy_bind_off && old->lazy_bind_off >= loff)
			roff = min(roff, old->lazy_bind_off);
	}

	if (unlikely(roff == UINT32_MAX)) {
		xnd_error("Invalid end for current offset: 0x%x\n", loff);
		xnd_abort();
	}

	return roff;
}

static inline size_t macho_sizeof_patched_bind_info(struct macho_info *info,
					            u32 old_bind_off)
{
	struct dyld_info_command *old = info->old_cmd, *new = info->cmd;

	if (old_bind_off == old->bind_off)
		return new->bind_size;
	else if (old_bind_off == old->weak_bind_off)
		return new->weak_bind_size;
	else if (old_bind_off == old->lazy_bind_off)
		return new->lazy_bind_size;

	xnd_error("Couldn't find bind info from offset 0x%x\n", old_bind_off);
	xnd_abort();
	unreachable();
}

/**
 * macho_serialize_opcodes:
 *  Serialize new bind opcode information, omitting arm64e specific opcodes
 *  when encountered.
 *
 * \info      Pointer to mach-o executable information
 * \bind_off  Offset of current bind opcode section to serialize
 * \return    Pointer to malloc'd buffer with serial bind opcode information
 */
static u8 *macho_serialize_opcodes(struct macho_info *info,
				   u32 old_bind_off, u32 new_bind_size)
{
	struct dyld_info_command  *old = info->old_cmd, *new = info->cmd;
	u8                        *buf, *itr, *next;
	u8                        *op_start, *op_end, imm, op;
	u32                       idx, step, bytes = 0;
	u32                       bind_size;

	if (old_bind_off == old->bind_off) {
		bind_size = old->bind_size;
		xnd_assert(new_bind_size == new->bind_size);
	} else if (old_bind_off == old->weak_bind_off) {
		bind_size = old->weak_bind_size;
		xnd_assert(new_bind_size == new->weak_bind_size);
	} else if (old_bind_off == old->lazy_bind_off) {
		bind_size = new->lazy_bind_size;
		xnd_assert(new_bind_size == old->lazy_bind_size);
	}

	if ((buf = malloc(new_bind_size)) == NULL) {
		xnd_error("malloc: %s\n", strerror(errno));
		return NULL;
	}

	/**
	 * Find in-memory bind opcode range with original bind offset
	 * and size (difference from shift/arm64e opcode bytes)
	 */
	op_start = (u8 *)info->mh + old_bind_off;
	op_end = op_start + bind_size;

	itr = op_start;
	while (itr != op_end) {
		xnd_assert(bytes < new_bind_size);

		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		if (op != BIND_OPCODE_THREADED)
			*(buf + bytes++) = *itr++;

		switch (op) {
		case BIND_OPCODE_DONE:
			goto done;
		case BIND_OPCODE_DO_BIND:
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
		case BIND_OPCODE_SET_TYPE_IMM:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
			break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			while (*itr != '\0')
				*(buf + bytes++) = *itr++;
			*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			next = decode_sleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
		case BIND_OPCODE_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			next = decode_uleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			next = decode_uleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			next = decode_uleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_THREADED:
			/**
			 * Skip over threaded opcodes
			 *
			 * macho_read_threaded_subopcode will automatically
			 * advance iterator past any additional data
			 * associated with this opcode.
			 */
			itr++;
			macho_read_threaded_subopcode(&itr, imm);
			break;
		default:
			xnd_error("Unknown bind opcode: 0x%x\n", op | imm);
			return NULL;
		}
	}

done:
	if (bytes != new_bind_size)
		xnd_warn("Expected %u, got %u bytes\n", new_bind_size, bytes);

	return buf;
}

/**
 * macho_write_patched_executable:
 *  Write out patched arm64 executable with arm64e bind opcodes filtered
 *  out. Executable sections without bind opcodes can be written directly
 *  to \outfd. Bind opcode sections must have each byte filtered to omit
 *  arm64e specific opcodes before the section is serialized.
 *
 * \outfd   File descriptor to write executable to
 * \info    mach-o executable information (header, dyld_info_command, etc.)
 * \return  0 if serialization is successful, -1 otherwise
 */
static int macho_write_patched_executable(struct macho_info *info,
					  int outfd, size_t total_size)
{
	struct dyld_info_command  *cmd = info->cmd;
	u32                       idx, loff, roff, seqs = 0;
	u8                        *buf;
	int                       err;
	bool                      has_opcodes;
	void                      *addr;
	size_t                    nbyte;

	/**
	 * For each bind opcode section, if the section is not empty and
	 * arm64e opcodes need to be removed, account for another opcode
	 * range that can't be directly written out to temporary executable.
	 */
	if (cmd->bind_off && info->bind_shift)
		seqs++;
	if (cmd->weak_bind_off && info->weak_bind_shift)
		seqs++;
	if (cmd->lazy_bind_off && info->lazy_bind_shift)
		seqs++;

	seqs = (seqs << 1) | 1;
	for (idx = 0; idx < seqs; idx++) {
		has_opcodes = (idx & 0x1);
		loff = (idx == 0 ? 0 : roff);
		roff = (idx + 1 == seqs ? total_size :
			macho_section_end(info, loff, has_opcodes));
		if (has_opcodes) {
			xnd_assert(roff != total_size);
			nbyte = macho_sizeof_patched_bind_info(info, loff);
			buf = macho_serialize_opcodes(info, loff, nbyte);
			if (!buf) {
				xnd_error("macho_serialize_opcodes error\n");
				return -1;
			}
			err = (writeall(outfd, buf, nbyte) != nbyte);
			free(buf);
			if (err) {
				xnd_error("Failed to write bind opcodes\n"
					  "(idx=%u, seqs=%u)\n", idx, seqs);
				return -1;
			}
		} else {
			addr = (void *)((char *)info->mh + loff);
			nbyte = (size_t)(roff - loff);
			if (writeall(outfd, addr, nbyte) != nbyte) {
				xnd_error("Failed to write executable\n"
					  "(idx=%u, seqs=%u)\n", idx, seqs);
				return -1;
			}
		}
	}

	return 0;
}

static int arm64e_with_dyld_info_to_arm64(int outfd, void *mh, size_t size,
					  struct dyld_info_command *cmd)
{
	struct macho_info  info = { .mh = mh, .cmd = cmd, 0 };
	int                err;

	/**
	 * Determine total size in bytes of arm64e specific opcodes that
	 * will be omitted from patched executable. Results are stored in
	 * info.
	 *
	 * info.bind_shift = sizeof arm64e opcodes in bind info
	 * info.weak_bind_shift = sizeof arm64e opcodes in weak bind info
	 * info.lazy_bind_shift = sizeof arm64e opcodes in lazy bind info
	 */
	macho_sizeof_arm64e_opcodes(&info, BIND_TYPE_REGULAR);
	macho_sizeof_arm64e_opcodes(&info, BIND_TYPE_WEAK);
	macho_sizeof_arm64e_opcodes(&info, BIND_TYPE_LAZY);

	info.old_cmd = malloc(sizeof(*cmd));
	memcpy(info.old_cmd, cmd, sizeof(*cmd));

	/**
	 * Shift any relative offsets stored in load commands (LC_*) to
	 * account for the number of bytes that will be omitted in the
	 * patched executable.
	 *
	 * dyld_info_command (LC_DYLD_INFO(_ONLY)) offset and size fields
	 * must be adjusted as well.
	 */
	err = macho_shift_offsets(&info);
	macho_shift_dyld_info(&info);
	if (err != 0) {
		xnd_error("Failed to shift load command offsets\n");
		return -1;
	}

	err = macho_write_patched_executable(&info, outfd, size);
	if (err != 0) {
		xnd_error("Failed to write patched arm64 executable\n");
		return -1;
	}

	return 0;
}
	
int binary_arm64e_to_arm64(char *path, char *tmp)
{
        int                       srcfd = -1, dstfd = -1;
	int                       err, ret = ARM64E_TO_ARM64_SUCCESS;
        void                      *mh, *addr = NULL;
        size_t                    size, nbyte;
        off_t                     off;
        u32                       magic;
	struct load_command       *lc;

        if ((srcfd = open(path, O_RDONLY)) < 0) {
                xnd_error("open: %s\n", strerror(errno));
		ret = ARM64E_TO_ARM64_FAILURE;
                goto out;
        }

        if ((off = lseek(srcfd, 0, SEEK_END)) < 0) {
                xnd_error("lseek: %s\n", strerror(errno));
		ret = ARM64E_TO_ARM64_FAILURE;
                goto out;
        }

	size = (size_t)off;
        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, srcfd, 0);
        if (addr == MAP_FAILED) {
                xnd_error("mmap: %s\n", strerror(errno));
		ret = ARM64E_TO_ARM64_FAILURE;
                goto out;
        }

	/* Exit if binary is not an arm64e compiled executable */
        if (!binary_is_arm64e(addr)) {
		ret = ARM64E_TO_ARM64_NOT_ARM64E;
                goto out;
	}
        
        if ((dstfd = open(tmp, O_WRONLY | O_CREAT, 0755)) < 0) {
                xnd_error("open: %s\n", strerror(errno));
		ret = ARM64E_TO_ARM64_FAILURE;
                goto out;
        }
        
	magic = *(u32 *)addr;
	if (HEADER_IS_MACHO(magic)) {
		mh = macho_arm64e_to_arm64(addr);
	} else if (HEADER_IS_FAT(magic)) {
		mh = fat_arm64e_to_arm64(addr);
	} else {
		xnd_warn("Unrecognized binary format: %s\n", path);
		ret = ARM64E_TO_ARM64_FAILURE;
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
	 * If executable uses LC_DYLD_INFO(_ONLY) instead of
	 * LC_DYLD_CHAINED_FIXUPS and LC_DYLD_EXPORTS_TRIE, bind
	 * information needs to be filtered to remove arm64e specific
	 * bind opcodes.
	 */
	if ((lc = macho_find_load_command(mh, LC_DYLD_INFO)) ||
	    (lc = macho_find_load_command(mh, LC_DYLD_INFO_ONLY))) {
		struct dyld_info_command *cmd =
			(struct dyld_info_command *)lc;
		err = arm64e_with_dyld_info_to_arm64(dstfd, mh, nbyte, cmd);
		if (err != 0)
			ret = ARM64E_TO_ARM64_FAILURE;
	} else {
		if (writeall(dstfd, mh, nbyte) != nbyte)
			ret = ARM64E_TO_ARM64_FAILURE;
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
