/* xnd_macho_parse.c */
#include "xnd/xnd.h"
#include "xnd/util/path.h"
#include "common/leb128.h"
#include "macho.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define OPT_BIND_INFO 		0x1
#define OPT_WEAK_BIND_INFO	0x2
#define OPT_LAZY_BIND_INFO	0x4
#define OPT_FIXUP_CHAINS	0x8

static const char *help =
"USAGE: ./xnd_macho_parse [options] file\n"
"OPTIONS:\n"
"  --bind\n"
"     Parse bind opcode stream\n"
"  --weak-bind\n"
"     Parse weak bind opcode stream\n"
"  --lazy-bind\n"
"     Parse lazy bind opcode stream\n"
"  --fixup-chains\n"
"     Parse fixup chains (binds and rebases)\n";

static const char *bind_opcode_str[] = {
	[BIND_OPCODE_DONE] =
	"BIND_OPCODE_DONE",
	[BIND_OPCODE_SET_DYLIB_ORDINAL_IMM] =
	"BIND_OPCODE_SET_DYLIB_ORDINAL_IMM",
	[BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB] =
	"BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB",
	[BIND_OPCODE_SET_DYLIB_SPECIAL_IMM] =
	"BIND_OPCODE_SET_DYLIB_SPECIAL_IMM",
	[BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM] =
	"BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM",
	[BIND_OPCODE_SET_TYPE_IMM] =
	"BIND_OPCODE_SET_TYPE_IMM",
	[BIND_OPCODE_SET_ADDEND_SLEB] =
	"BIND_OPCODE_SET_ADDEND_SLEB",
	[BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB] =
	"BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB",
	[BIND_OPCODE_ADD_ADDR_ULEB] =
	"BIND_OPCODE_ADD_ADDR_ULEB",
	[BIND_OPCODE_DO_BIND] =
	"BIND_OPCODE_DO_BIND",
	[BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB] =
	"BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB",
	[BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED] =
	"BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED",
	[BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB] =
	"BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB",
	[BIND_OPCODE_THREADED] =
	"BIND_OPCODE_THREADED"
};

static const char *bind_subopcode_str[] = {
	[BIND_SUBOPCODE_THREADED_APPLY] =
	"BIND_SUBOPCODE_THREADED_APPLY",
	[BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB] =
	"BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB"
};

static const char *bind_type_str[] = {
	[BIND_TYPE_POINTER] =
	"BIND_TYPE_POINTER",
	[BIND_TYPE_TEXT_ABSOLUTE32] =
	"BIND_TYPE_TEXT_ABSOLUTE32",
	[BIND_TYPE_TEXT_PCREL32] =
	"BIND_TYPE_TEXT_PCREL32"
};

static const char *bind_flags_str[] = {
	[0] = "NONE",
	[BIND_SYMBOL_FLAGS_WEAK_IMPORT] =
	"BIND_SYMBOL_FLAGS_WEAK_IMPORT",
	[BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION] =
	"BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION"
};

static const char *pac_key_str[] = {
	[0b00] = "APIAKey_EL1",
	[0b01] = "",
	[0b10] = "",
	[0b11] = ""
};

union dyld_chained_ptr_arm64e {
	u64 value;
	struct dyld_chained_ptr_arm64e_bind bind;
	struct dyld_chained_ptr_arm64e_auth_bind auth_bind;
	struct dyld_chained_ptr_arm64e_rebase rebase;
	struct dyld_chained_ptr_arm64e_auth_rebase auth_rebase;
};

struct fixup_entry {
	void *addr;
	char *symbol;
	long lib_ordinal;
	s64 addend;
	u32 seg_index;
	u32 flags;
	u8 type;
	bool special;
};

struct macho_all_info {
	const char *path;
	struct mach_header *mh;
	struct dyld_info_command *cmd;
	int fd;
	size_t size;
};

static void usage(void);
static char *macho_dylib_name(void *, long, bool);
static char *macho_segment_name(void *, u32);
static void macho_print_threaded_opcode(u8 **, u8, char *, u64);
static int macho_print_opcodes(struct macho_all_info *, int);
static u64 macho_bind_table_size(struct macho_all_info *);
static void macho_print_fixup(struct macho_all_info *,
			      struct fixup_entry *);
static int macho_walk_fixup_chain(struct macho_all_info *, u32, u64,
				  struct fixup_entry *, u64,
				  struct fixup_entry **, u64 *, u64 *);
static int macho_parse_fixup_chains(struct macho_all_info *);

int main(int argc, char *argv[])
{
	int err, exc, fd = -1, options = 0;
	char *file = NULL, path[PATH_MAX] = {0};
	void *mh, *cmd, *addr = NULL;
	size_t size;
	off_t offset;
	u32 magic;
	struct macho_all_info info;

	xnd_log_setup();
	if (argc < 2) {
		usage();
		exit(0);
	}

	argv++;
	argc--;
	while (argc) {
		if (strcmp(argv[0], "--bind") == 0) {
			options |= OPT_BIND_INFO;
			argv++;
			argc--;
		} else if (strcmp(argv[0], "--weak-bind") == 0) {
			options |= OPT_WEAK_BIND_INFO;
			argv++;
			argc--;
		} else if (strcmp(argv[0], "--lazy-bind") == 0) {
			options |= OPT_LAZY_BIND_INFO;
			argv++;
			argc--;
		} else if (strcmp(argv[0], "--fixup-chains") == 0) {
			options |= OPT_FIXUP_CHAINS;
			argv++;
			argc--;
		} else if (strncmp(argv[0], "--", 2) == 0) {
			xnd_error("Unknown option: %s\n", argv[0]);
			usage();
			exit(-1);
		} else {
			file = argv[0];
			argv++;
			argc--;
		}
	}

	if (!file) {
		xnd_error("No mach-o file provided\n");
		usage();
		exit(-1);
	} else if (!options) {
		xnd_error("No options selected\n");
		usage();
		exit(-1);
	}

	err = access(file, X_OK);
	if (err != 0) {
		if (errno != ENOENT) {
			xnd_error("Invalid executable: %s\n", file);
			exit(-1);
		}
		err = xnd_path_find(file, path, PATH_MAX);
		if (err != 0) {
			xnd_error("Couldn't find executable: %s\n", file);
			exit(-1);
		}
	} else {
		strncpy(path, file, strlen(file) + 1);
	}

	if ((fd = open(path, O_RDONLY)) < 0) {
		xnd_perror("open");
		exit(-1);
	}

	if ((offset = lseek(fd, 0, SEEK_END)) < 0) {
		xnd_perror("lseek");
		exc = -1;
		goto out;
	}

	size = (size_t)offset;
	addr = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		xnd_perror("mmap");
		exc = -1;
		goto out;
	}

	magic = *(u32 *)addr;
	if (HEADER_IS_FAT(magic)) {
		mh = macho_thin_from_fat(addr, CPU_TYPE_ARM64);
		if (mh == NULL) {
			exc = -1;
			goto out;
		}
	} else {
		mh = addr;
	}

	cmd = (void *)macho_find_load_command(mh, LC_DYLD_INFO_ONLY);
	if (cmd == NULL) {
		xnd_printf("No LC_DYLD_INFO_ONLY in %s\n", path);
		exc = -1;
		goto out;
	}

	info.path = path;
	info.mh = (struct mach_header *)mh;
	info.cmd = (struct dyld_info_command *)cmd;
	info.fd = fd;
	info.size = size;

	if (options & OPT_BIND_INFO) {
		err = macho_print_opcodes(&info, OPT_BIND_INFO);
		if (err != 0) {
			exc = -1;
			goto out;
		}
	}

	if (options & OPT_WEAK_BIND_INFO) {
		err = macho_print_opcodes(&info, OPT_WEAK_BIND_INFO);
		if (err != 0) {
			exc = -1;
			goto out;
		}
	}

	if (options & OPT_LAZY_BIND_INFO) {
		err = macho_print_opcodes(&info, OPT_LAZY_BIND_INFO);
		if (err != 0) {
			exc = -1;
			goto out;
		}
	}

	if (options & OPT_FIXUP_CHAINS) {
		err = macho_parse_fixup_chains(&info);
		if (err != 0) {
			exc = -1;
			goto out;
		}
	}

	exc = 0;
out:
	if (fd != -1)
		close(fd);
	if (addr && addr != MAP_FAILED)
		munmap(addr, size);

	exit(exc);
}

static void usage(void)
{
	xnd_printf("%s", help);
}

static char *macho_dylib_name(void *mh_addr, long ordinal, bool special)
{
	struct mach_header *mh;
	struct load_command *lc;
	struct dylib *dylib;
	uintptr_t lc_start, offset;
	u32 cmdsize;
	char *name;
	long idx = 1;

	if (special) {
		switch (ordinal) {
		case 0:
			return "BIND_SPECIAL_DYLIB_SELF";
		case -1:
			return "BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE";
		case -2:
			return "BIND_SPECIAL_DYLIB_FLAT_LOOKUP";
		default:
			xnd_error("Unknown special dylib: %ld\n", ordinal);
			return NULL;
		}
	}

	mh = (struct mach_header *)mh_addr;
	lc_start = (HEADER_IS_64BIT(mh->magic) ?
		    (uintptr_t)mh_addr + sizeof(struct mach_header_64) :
		    (uintptr_t)mh_addr + sizeof(struct mach_header));

	for (offset = 0; offset < mh->sizeofcmds; offset += cmdsize) {
		lc = (struct load_command *)(lc_start + offset);
		cmdsize = lc->cmdsize;
		if (lc->cmd != LC_LOAD_DYLIB &&
		    lc->cmd != LC_LOAD_WEAK_DYLIB &&
		    lc->cmd != LC_REEXPORT_DYLIB)
			continue;
		if (idx++ == ordinal) {
			dylib = &((struct dylib_command *)lc)->dylib;
			name = (char *)lc + dylib->name.offset;
			return name;
		}
	}

	xnd_error("Couldn't find dylib at index %ld\n", ordinal);
	return NULL;
}

static char *macho_segment_name(void *mh_addr, u32 seg_index)
{
	struct mach_header *mh;
	struct load_command *lc;
	uintptr_t lc_start, offset;
	u32 cmdsize, idx = 0;

	mh = (struct mach_header *)mh_addr;
	lc_start = (HEADER_IS_64BIT(mh->magic) ?
		    (uintptr_t)mh_addr + sizeof(struct mach_header_64) :
		    (uintptr_t)mh_addr + sizeof(struct mach_header));

	for (offset = 0; offset < mh->sizeofcmds; offset += cmdsize) {
		lc = (struct load_command *)(lc_start + offset);
		cmdsize = lc->cmdsize;
		if (lc->cmd != LC_SEGMENT && lc->cmd != LC_SEGMENT_64)
			continue;
		else if (idx++ == seg_index)
			return ((struct segment_command *)lc)->segname;
	}

	xnd_error("Couldn't find segment at index %u\n", seg_index);
	return NULL;
}

static void macho_print_threaded_opcode(u8 **p, u8 imm, char *indent,
				        u64 opcode_offset)
{
	u64 count;
	u8 *itr = *p;

	switch (imm) {
	case BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB:
		itr = decode_uleb128(itr, &count);
		printf("%s0x%llx %s: %llu\n", indent, opcode_offset,
		       bind_subopcode_str[imm], count);
		break;
	case BIND_SUBOPCODE_THREADED_APPLY:
		printf("%s0x%llx %s\n", indent, opcode_offset,
		       bind_subopcode_str[imm]);
		break;
	default:
		printf("Unknown threaded subopcode: 0x%x\n", imm);
		break;
	}

	*p = itr;
}

static int macho_print_opcodes(struct macho_all_info *info, int which)
{
	u8 *op_start, *op_end, *itr, op, imm, type;
	long ordinal;
	char *symbol, *dylib_name, *segname, indent[] = "    ";
	u32 flags, seg_index, nsymbols = 0, nopcodes = 0;
	u64 skip, count, opcode_offset, seg_offset;
	s64 addend;

	switch (which) {
	case OPT_BIND_INFO:
		op_start = (u8 *)info->mh + info->cmd->bind_off;
		op_end = op_start + info->cmd->bind_size;
		printf("Bind opcodes:\n");
		break;
	case OPT_WEAK_BIND_INFO:
		op_start = (u8 *)info->mh + info->cmd->weak_bind_off;
		op_end = op_start + info->cmd->weak_bind_size;
		printf("Weak bind opcodes:\n");
		break;
	case OPT_LAZY_BIND_INFO:
		op_start = (u8 *)info->mh + info->cmd->lazy_bind_off;
		op_end = op_start + info->cmd->lazy_bind_size;
		printf("Lazy bind opcodes:\n");
		break;
	default:
		xnd_error("Unrecognized option: %d\n", which);
		return -1;
	}

	itr = op_start;
	while (itr < op_end) {
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		opcode_offset = itr - op_start;
		itr++;
		nopcodes++;
		switch (op) {
		case BIND_OPCODE_DONE:
			printf("%s0x%llx %s\n",
			       indent, opcode_offset,
			       bind_opcode_str[op]);
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
			ordinal = imm;
			dylib_name = macho_dylib_name(
				info->mh, ordinal, false);
			printf("%s0x%llx %s: %ld (%s)\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], ordinal, dylib_name);
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
			itr = decode_uleb128(itr, (u64 *)&ordinal);
			dylib_name = macho_dylib_name(
				info->mh, ordinal, false);
			printf("%s0x%llx %s: %ld (%s)\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], ordinal, dylib_name);
			break;
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
			if ((ordinal = imm) != 0)
				ordinal |= BIND_OPCODE_MASK;
			dylib_name = macho_dylib_name(
				info->mh, ordinal, true);
			printf("%s0x%llx %s: %ld (%s)\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], ordinal, dylib_name);
			break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			nsymbols++;
			flags = imm;
			symbol = (char *)itr;
			while (*itr != '\0')
				itr++;
			itr++;
			printf("%s0x%llx %s: 0x%x, %s\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], flags, symbol);
			break;
		case BIND_OPCODE_SET_TYPE_IMM:
			type = imm;
			printf("%s0x%llx %s: %d\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], type);
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			itr = decode_sleb128(itr, &addend);
			printf("%s0x%llx %s: %lld\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], addend);
			break;
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			seg_index = imm;
			segname = macho_segment_name(info->mh, seg_index);
			itr = decode_uleb128(itr, &seg_offset);
			printf("%s0x%llx %s: 0x%x (%s), 0x%llx\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], seg_index,
			       segname, seg_offset);
			break;
		case BIND_OPCODE_ADD_ADDR_ULEB:
			itr = decode_uleb128(itr, &skip);
			printf("%s0x%llx %s: 0x%llx\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], skip);
			break;
		case BIND_OPCODE_DO_BIND:
			printf("%s0x%llx %s\n",
			       indent, opcode_offset,
			       bind_opcode_str[op]);
			break;
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
			itr = decode_uleb128(itr, &skip);
			printf("%s0x%llx %s: 0x%llx\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], skip);
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
			skip = imm * sizeof(void *) + sizeof(void *);
			printf("%s0x%llx %s: 0x%llx\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], skip);
			break;
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			itr = decode_uleb128(itr, &count);
			itr = decode_uleb128(itr, &skip);
			printf("%s0x%llx %s: %llu, 0x%llx\n",
			       indent, opcode_offset,
			       bind_opcode_str[op], count, skip);
			break;
		case BIND_OPCODE_THREADED:
			macho_print_threaded_opcode(
				&itr, imm, indent, opcode_offset);
			break;
		default:
			printf("Unknown opcode: 0x%x\n", op | imm);
			break;
		}
	}

	printf("\nTotol bind opcodes: %u\n"
	       "Total symbols binded: %u\n"
	       "Total size of bind info: %u\n\n",
	       nopcodes, nsymbols,
	       (which == OPT_BIND_INFO ? info->cmd->bind_size :
		which == OPT_WEAK_BIND_INFO ? info->cmd->weak_bind_size :
		info->cmd->lazy_bind_size));

	return 0;
}

static u64 macho_bind_table_size(struct macho_all_info *info)
{
	u8 *itr, *op_start, *op_end, op, imm;
	u64 size = 0;
	bool found = false;

	op_start = (u8 *)info->mh + info->cmd->bind_off;
	op_end = op_start + info->cmd->bind_size;

	itr = op_start;
	while (itr != op_end) {
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		itr++;
		if (op == BIND_OPCODE_THREADED) {
			found = true;
			break;
		}
	}

	if (!found) {
		xnd_error("Couldn't find bind table size\n");
		xnd_abort();
	}

	switch (imm) {
	case BIND_SUBOPCODE_THREADED_APPLY:
		xnd_error("Unexpected BIND_SUBOPCODE_THREADED_APPLY");
		xnd_abort();
	case BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB:
		itr = decode_uleb128(itr, &size);
		return size;
	default:
		xnd_error("Unknown subopcode: 0x%x\n", imm);
		xnd_abort();
	}

	unreachable();
}

static void macho_print_fixup(struct macho_all_info *info,
			      struct fixup_entry *fixup)
{
	union dyld_chained_ptr_arm64e ptr;
	const char *segname, *dylib, *flags, *type;
	u8 auth_and_bind = 0;

	ptr.value = *(u64 *)fixup->addr;
	segname = macho_segment_name(info->mh, fixup->seg_index);

	auth_and_bind |= ((ptr.value & (1ull << 63)) != 0 ? 0b10 : 0b00);
	auth_and_bind |= ((ptr.value & (1ull << 62)) != 0 ? 0b01 : 0b00);
	switch (auth_and_bind) {
	case 0b11: {
		/**
		 * Authenticated bind:
		 *  dyld_chained_ptr_arm64e_auth_bind
		 */
		type = bind_type_str[fixup->type];
		flags = bind_flags_str[fixup->flags];
		dylib = macho_dylib_name(
			info->mh, fixup->lib_ordinal, fixup->special);
		printf("\tdyld_chained_ptr_arm64e_auth_bind:\n"
		       "\t      value: 0x%llx\n"
		       "\t    ordinal: %u\n"
		       "\t  diversity: %u\n"
		       "\t    addrDiv: %s\n"
		       "\t        key: %s\n"
		       "\t       next: %u\n"
		       "\t      dylib: %s\n"
		       "\t    segment: %s\n"
		       "\t     symbol: %s\n"
		       "\t      flags: %s\n"
		       "\t       type: %s\n"
		       "\t     addend: %lld\n",
		       ptr.value, (u16)ptr.auth_bind.ordinal,
		       (u16)ptr.auth_bind.diversity,
		       (ptr.auth_bind.addrDiv ? "true" : "false"),
		       pac_key_str[ptr.auth_bind.key],
		       (u16)ptr.auth_bind.next, dylib, segname,
		       fixup->symbol, flags, type, fixup->addend);
		break;
	}
	case 0b10:
		/**
		 * Authenticated rebase:
		 *  dyld_chained_ptr_arm64e_auth_rebase.
		 */
		printf("\tdyld_chained_ptr_arm64e_auth_rebase:\n"
		       "\t      value: 0x%llx\n"
		       "\t     target: 0x%x\n"
		       "\t  diversity: %u\n"
		       "\t    addrDiv: %s\n"
		       "\t        key: %s\n"
		       "\t       next: %u\n"
		       "\t    segment: %s\n",
		       ptr.value, (u32)ptr.auth_rebase.target,
		       (u16)ptr.auth_rebase.diversity,
		       (ptr.auth_rebase.addrDiv ? "true" : "false"),
		       pac_key_str[ptr.auth_rebase.key],
		       (u16)ptr.auth_rebase.next, segname);
		break;
	case 0b01: {
		/**
		 * Unauthenticated bind:
		 *  dyld_chained_ptr_arm64e_bind:
		 */
		type = bind_type_str[fixup->type];
		flags = bind_flags_str[fixup->flags];
		dylib = macho_dylib_name(
			info->mh, fixup->lib_ordinal, fixup->special);
		printf("\tdyld_chained_ptr_arm64e_bind:\n"
		       "\t      value: 0x%llx\n"
		       "\t    ordinal: %u\n"
		       "\t     addend: %u\n"
		       "\t       next: %u\n"
		       "\t      dylib: %s\n"
		       "\t    segment: %s\n"
		       "\t     symbol: %s\n"
		       "\t      flags: %s\n"
		       "\t       type: %s\n"
		       "\t     addend: %lld\n",
		       ptr.value, (u16)ptr.bind.ordinal,
		       (u32)ptr.bind.addend, (u16)ptr.bind.next,
		       dylib, segname, fixup->symbol,
		       flags, type, fixup->addend);
		break;
	}
	case 0b00:
		/**
		 * Unauthenticated rebase:
		 *  dyld_chained_ptr_arm64e_rebase.
		 */
		printf("\tdyld_chained_ptr_arm64e_rebase:\n"
		       "\t      value: 0x%llx\n"
		       "\t     target: 0x%llx\n"
		       "\t      high8: %u\n"
		       "\t       next: %u\n"
		       "\t    segment: %s\n",
		       ptr.value, (u64)ptr.rebase.target,
		       (u8)ptr.rebase.high8, (u16)ptr.rebase.next, segname);
		break;
	default:
		break;
	}
}

static int macho_walk_fixup_chain(struct macho_all_info *info,
				  u32 seg_index, u64 seg_offset,
				  struct fixup_entry *ordinals, u64 nordinals,
				  struct fixup_entry **chain, u64 *chain_cap,
				  u64 *chain_count)
{
	void *seg;
	u32 cmd;
	uintptr_t offset;
	u64 value, ordinal, next;
	bool is_bind;
	struct fixup_entry *entry, *grown, *ord;

	if ((seg = macho_find_segment(info->mh, seg_index)) == NULL)
		return -1;

	cmd = *(u32 *)seg;
	switch (cmd) {
	case LC_SEGMENT:
		offset = ((struct segment_command *)seg)->fileoff;
		break;
	case LC_SEGMENT_64:
		offset = ((struct segment_command_64 *)seg)->fileoff;
		break;
	default:
		xnd_error("Unknown load command: %u\n", cmd);
		return -1;
	}

	offset += seg_offset;
	for (;;) {
		if (*chain_count == *chain_cap) {
			*chain_cap *= 2;
			grown = realloc(
				*chain, *chain_cap * sizeof(**chain));
			if (grown == NULL)
				return -1;
			*chain = grown;
		}

		entry = &(*chain)[(*chain_count)++];
		memset(entry, 0, sizeof(*entry));
		entry->addr = (void *)((u8 *)info->mh + offset);
		entry->seg_index = seg_index;

		value = *(u64 *)entry->addr;
		is_bind = (value & (1ULL << 62)) != 0;

		if (is_bind) {
			ordinal = value & 0xFFFFULL;
			if (ordinal >= nordinals) {
				xnd_error("Bind ordinal %llu out of range "
					  "(table size %llu)\n",
					  ordinal, nordinals);
				return -1;
			} else {
				ord = &ordinals[ordinal];
				entry->symbol = ord->symbol;
				entry->lib_ordinal = ord->lib_ordinal;
				entry->special = ord->special;
				entry->addend = ord->addend;
				entry->type = ord->type;
				entry->flags = ord->flags;
			}
		}

		if ((next = ((value >> 51) & 0x7FFULL)) == 0)
			break;

		offset += next * 8;
	}

	return 0;
}

static int macho_parse_fixup_chains(struct macho_all_info *info)
{
	int ret = 0;
	u8 *itr, *op_start, *op_end, op, imm, type = 0;
	u32 seg_index = 0, flags = 0;
	u64 table_size, table_idx = 0, seg_offset = 0;
	u64 chain_cap, chain_count = 0;
	s64 addend = 0;
	long lib_ordinal = 0;
	char *symbol = NULL;
	bool special = false;
	struct fixup_entry *ordinals, *chain, *entry;

	table_size = macho_bind_table_size(info);
	ordinals = calloc(table_size, sizeof(*ordinals));
	if (ordinals == NULL)
		return -1;

	chain_cap = 256;
	chain = malloc(chain_cap * sizeof(*chain));
	if (chain == NULL) {
		free(ordinals);
		return -1;
	}

	op_start = (u8 *)info->mh + info->cmd->bind_off;
	op_end = op_start + info->cmd->bind_size;

	itr = op_start;
	while (itr != op_end) {
		op = *itr & BIND_OPCODE_MASK;
		imm = *itr & BIND_IMMEDIATE_MASK;
		itr++;
		switch (op) {
		case BIND_OPCODE_DONE:
			break;
		case BIND_OPCODE_DO_BIND:
			entry = &ordinals[table_idx++];
			entry->lib_ordinal = lib_ordinal;
			entry->special = special;
			entry->symbol = symbol;
			entry->flags = flags;
			entry->addend = addend;
			entry->type = type;
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
			lib_ordinal = (long)imm;
			special = false;
			break;
		case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
			itr = decode_uleb128(itr, (u64 *)&lib_ordinal);
			special = false;
			break;
		case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
			lib_ordinal = (imm == 0 ? imm :
				       (long)((s8)imm | BIND_OPCODE_MASK));
			special = true;
			break;
		case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
			flags = (u32)imm;
			symbol = (char *)itr;
			while (*itr != '\0')
				itr++;
			itr++;
			break;
		case BIND_OPCODE_SET_TYPE_IMM:
			type = imm;
			break;
		case BIND_OPCODE_SET_ADDEND_SLEB:
			itr = decode_sleb128(itr, &addend);
			break;
		case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
			seg_index = (u32)imm;
			itr = decode_uleb128(itr, &seg_offset);
			break;
		case BIND_OPCODE_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
		case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
		case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
			xnd_error("Unhandled opcode: %s\n",
				  bind_opcode_str[op]);
			xnd_abort();
		case BIND_OPCODE_THREADED:
			if (imm != BIND_SUBOPCODE_THREADED_APPLY) {
				itr = decode_uleb128(itr, NULL);
				break;
			}
			ret = macho_walk_fixup_chain(
				info, seg_index, seg_offset,
				ordinals, table_idx,
				&chain, &chain_cap, &chain_count);
			if (ret != 0)
				goto out;
			break;
		default:
			xnd_error("Unknown opcode: 0x%x\n", op | imm);
			ret = -1;
			goto out;
		}
	}

	for (u64 i = 0; i < chain_count; i++) {
		printf("Fixup #%llu:\n", i);
		macho_print_fixup(info, &chain[i]);
		printf("\n");
	}
out:
	free(chain);
	free(ordinals);
	return ret;
}
