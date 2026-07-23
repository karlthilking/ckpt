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
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static inline bool fat_is_arm64e(void *);
static inline bool macho_is_arm64e(void *);
static inline bool binary_is_arm64e(void *);
static inline void *fat_arm64e_to_arm64(void *);
static inline void *macho_arm64e_to_arm64(void *);

static inline u32 macho_next_offset(struct macho_info *, u32, bool);
static inline void macho_skip_threaded_opcode(u8 **);
static u8 *macho_serialize_opcodes(struct macho_info *, u32);
static int arm64e_with_dyld_info_to_arm64(int, void *, size_t,
					  struct dyld_info_command *);

static inline bool fat_is_arm64e(void *fh_addr)
{
	struct fat_header  *fh;
	struct fat_arch	   *fa;
	u32		   nfat_arch;
	s32		   cputype, subtype;
	bool		   bswap;

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
	struct mach_header_64  *mh;
	s32		       cputype, subtype;

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
	struct fat_header      *fh;
	struct fat_arch	       *fa;
	struct mach_header_64  *mh;
	u32		       nfat_arch, offset;
	s32		       cputype, subtype;
	bool		       bswap;

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
	struct mach_header_64  *mh;
	s32		       cputype, subtype;

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

static inline u32 macho_next_offset(struct macho_info *info, u32 curr,
				    bool has_opcodes)
{
	struct dyld_info_command    	*cmd = info->cmd;
	u32				next = UINT32_MAX;

	if (has_opcodes) {
		if (curr == cmd->bind_off)
			next = cmd->bind_off + cmd->bind_size;
		else if (curr == cmd->weak_bind_off)
			next = cmd->weak_bind_off + cmd->weak_bind_size;
		else if (curr == cmd->lazy_bind_off)
			next = cmd->lazy_bind_off + cmd->lazy_bind_size;
	} else {
		if (cmd->bind_off && cmd->bind_off >= curr)
			next = min(next, cmd->bind_off);
		if (cmd->weak_bind_off && cmd->weak_bind_off >= curr)
			next = min(next, cmd->weak_bind_off);
		if (cmd->lazy_bind_off && cmd->lazy_bind_off >= curr)
			next = min(next, cmd->lazy_bind_off);
	}

	if (unlikely(next == UINT32_MAX)) {
		xnd_error("Error finding next offset: 0x%x\n", curr);
		xnd_abort();
	}

	return next;
}

static inline void macho_append_offset_binds(u8 *op_start, u8 *op_end,
					     u8 *buf, u32 *bytesp)
{
	u32 idx, step, bytes = *bytesp;
	u8 op, imm, *next, *itr = op_start;

	while (op_start < op_end) {
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		if (op != BIND_OPCODE_THREADED)
			itr++;
		switch (op) {
		case BIND_OPCODE_DONE:
			goto done;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			while (*itr != '\0')
				itr++;
			itr++;
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			itr = decode_sleb128(itr, NULL);
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		case BIND_OPCODE_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			itr = decode_uleb128(itr, NULL);
			break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			itr = decode_uleb128(itr, NULL);
			itr = decode_uleb128(itr, NULL);
			break;
		case BIND_OPCODE_THREADED:
			macho_skip_threaded_opcode(&itr);
			break;
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			/**
			 * Append segment and offset bind info to
			 * end of buffer
			 */
			*(buf + bytes++) = op | imm;
			next = decode_uleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		default:
			break;
		}
	}

done:
	*bytesp = bytes;
	return;
}

static inline void macho_skip_threaded_opcode(u8 **p)
{
	u8 *next, *itr, op, imm;

	itr = *p;
	op = *itr & BIND_OPCODE_MASK;
	if (unlikely(op != BIND_OPCODE_THREADED)) {
		xnd_error("Not BIND_OPCODE_THREADED: 0x%x\n", op);
		xnd_abort();
	}

	imm = *itr & BIND_IMMEDIATE_MASK;
	itr++;
	switch (imm) {
	case BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB:
		next = decode_uleb128(itr, NULL);
		break;
	case BIND_SUBOPCODE_THREADED_APPLY:
		next = itr;
		break;
	default:
		xnd_error("Unknown threaded bind subopcode: 0x%x\n", imm);
		xnd_abort();
		unreachable();
	}

	*p = next;
}

static u8 *macho_serialize_opcodes(struct macho_info *info, u32 bind_off)
{
	struct dyld_info_command *cmd = info->cmd;
	u8 *buf, *op_start, *op_end, *itr, *next, imm, op;
	u32 idx, step, bind_size, bytes = 0;
	bool first_bind = true;

	if (bind_off == cmd->bind_off) {
		bind_size = cmd->bind_size;
	} else if (bind_off == cmd->weak_bind_off) {
		bind_size = cmd->weak_bind_off;
	} else if (bind_off == cmd->lazy_bind_off) {
		bind_size = cmd->lazy_bind_off;
	} else {
		xnd_error("No bind info for offset 0x%x\n", bind_off);
		return NULL;
	}

	if ((buf = malloc(bind_size)) == NULL) {
		xnd_error("malloc: %s\n", strerror(errno));
		return NULL;
	}

	op_start = (u8 *)info->mh + bind_off;
	op_end = op_start + bind_size;
	itr = op_start;
	while (itr != op_end && bytes != bind_size) {
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		switch (op) {
		case BIND_OPCODE_DONE:
			*(buf + bytes++) = *itr++;
			goto done;
		case BIND_OPCODE_DO_BIND:
			if (first_bind) {
				macho_append_offset_binds(op_start, op_end,
							  buf, &bytes);
				first_bind = false;
			}
			*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
		case BIND_OPCODE_SET_TYPE_IMM:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
			/* No extra data */
			*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			/* Copy string to buffer */
			*(buf + bytes++) = *itr++;
			while (*itr != '\0')
				*(buf + bytes++) = *itr++;
			*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			/* Copy encoded sleb128 to buffer */
			*(buf + bytes++) = *itr++;
			next = decode_sleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
		case BIND_OPCODE_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			/* Copy uleb128 */
			*(buf + bytes++) = *itr++;
			next = decode_uleb128(itr, NULL);
			step = next - itr;
			for (idx = 0; idx < step; idx++)
				*(buf + bytes++) = *itr++;
			break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			/* Consume two encoded uleb128's */
			*(buf + bytes++) = *itr++;
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
			 * Skip over threaded opcodes including any
			 * extra data associated with the opcode
			 */
			macho_skip_threaded_opcode(&itr);
			break;
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			/**
			 * Already appended before first
			 * BIND_OPCODE_DO_BIND, skip over here
			 */
			itr = decode_uleb128(itr, NULL);
			break;
		default:
			xnd_error("Unknown bind opcode: 0x%x\n", op | imm);
			return NULL;
		}
	}

done:
	/* Pad buffer */
	while (bytes < bind_size)
		*(buf + bytes++) = BIND_OPCODE_DONE;

	return buf;
}

static int arm64e_with_dyld_info_to_arm64(int outfd, void *mh, size_t size,
					  struct dyld_info_command *cmd)
{
	struct macho_info 	info = { .mh = mh, .cmd = cmd };
	int			err;
	u8			*buf;
	u32			idx, vec[2], len = 0;
	bool			has_opcodes;
	void			*addr;
	size_t			nbyte;

	if (cmd->bind_off)
		len++;
	if (cmd->weak_bind_off)
		len++;
	if (cmd->lazy_bind_off)
		len++;

	len = (len << 1) | 1;
	for (idx = 0; idx < len; idx++) {
		has_opcodes = (idx & 0x1);
		vec[0] = (idx == 0 ? 0 : vec[1]);
		vec[1] = (idx + 1 == len ? size :
			  macho_next_offset(&info, vec[0], has_opcodes));
		nbyte = (size_t)(vec[1] - vec[0]);
		if (has_opcodes) {
			xnd_assert(vec[1] != size);
			buf = macho_serialize_opcodes(&info, vec[0]);
			if (!buf) {
				xnd_error("macho_serialize opcodes error\n");
				return -1;
			}
			err = (writeall(outfd, buf, nbyte) != nbyte);
			free(buf);
			if (err) {
				xnd_error("Error writing bind opcodes\n");
				return -1;
			}
		} else {
			addr = (void *)((char *)mh + vec[0]);
			if (writeall(outfd, addr, nbyte) != nbyte) {
				xnd_error("Error writing executable data\n");
				return -1;
			}
		}
	}

	return 0;
}

int binary_arm64e_to_arm64(char *path, char *tmp)
{
	int srcfd = -1, dstfd = -1;
	int err, ret = ARM64E_TO_ARM64_SUCCESS;
	struct load_command *lc;
	void *mh, *addr = NULL;
	size_t size, nbyte;
	off_t off;
	u32 magic;

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
	addr = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, srcfd, 0);
	if (addr == MAP_FAILED) {
		xnd_error ("mmap: %s\n", strerror (errno));
		ret = ARM64E_TO_ARM64_FAILURE;
		goto out;
	}

	/* Exit if binary is not an arm64e compiled executable */
	if (!binary_is_arm64e(addr)) {
		ret = ARM64E_TO_ARM64_NOT_ARM64E;
		goto out;
	}

	if ((dstfd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0) {
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
		struct dyld_info_command *cmd = (struct dyld_info_command *)lc;
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

/**
 * macho_find_load_command:
 *  Find specific load command in memory-mapped mach-o executable
 *
 * \mh_addr  Address of executable's mach header
 * \cmd	     Load command to search for
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
	ncmds = (bswap ? __builtin_bswap32(mh->ncmds) : mh->ncmds);

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
	nfat_arch = (bswap ? __builtin_bswap32(fh->nfat_arch) :
		     fh->nfat_arch);

	fa = (struct fat_arch *)((char *)fh + sizeof(*fh));
	for (u32 idx = 0; idx < nfat_arch; idx++, fa++) {
		cputype = (bswap ? __builtin_bswap32(fa->cputype) :
			   fa->cputype);
		if (cputype == arch) {
			found = true;
			break;
		}
	}

	if (!found) {
		xnd_error("Couldn't find slice with arch %x", arch);
		return NULL;
	}

	offset = (bswap ? __builtin_bswap32(fa->offset) : fa->offset);
	return (void *)((char *)fh + offset);
}
