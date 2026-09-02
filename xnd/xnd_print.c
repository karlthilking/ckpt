/* xnd_print.c */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <ucontext.h>
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/pac.h"
#include "xnd/vm_region.h"
#include "xnd/util/compress.h"
#include "xnd/util/path.h"
#include "xnd/util/io.h"

#define KILOBYTES(bytes) \
	(((float)(bytes)) / 1024.0f)
#define MEGABYTES(bytes) \
	(KILOBYTES(bytes) / 1024.0f)
#define GIGABYTES(bytes) \
	(MEGABYTES(bytes) / 1024.0f)

#define ARG_IS_CKPT(arg) \
        (strstr(arg, XND_CKPTFILE_SUFFIX) != NULL)
#define ARG_IS_COMPRESSED_CKPT(arg) \
        (strstr(arg, XND_COMPRESSED_SUFFIX) != NULL)
#define ARG_IS_ALL(arg) \
        (strncmp(arg, "-a", sizeof("-a")) == 0 || \
         strncmp(arg, "--all", sizeof("--all")) == 0)
#define ARG_IS_REGIONS(arg) \
        (strncmp(arg, "--regions", sizeof("--regions")) == 0)
#define ARG_IS_REGION_INFO(arg) \
        (strncmp(arg, "--region-info", sizeof("--region-info")) == 0)
#define ARG_IS_PRINT_CONTEXT(arg) \
        (strncmp(arg, "--print-context", sizeof("--print-context")) == 0)

enum {
        PRINT_USER_CONTEXT,
        PRINT_TEXT_REGIONS,
        PRINT_DATA_REGIONS,
        PRINT_HEAP_REGIONS,
        PRINT_STACK_REGIONS,
        PRINT_OTHER_REGIONS
};

static bool print_options[] = {
        [PRINT_USER_CONTEXT]    = true,
        [PRINT_TEXT_REGIONS]    = true,
        [PRINT_DATA_REGIONS]    = true,
        [PRINT_HEAP_REGIONS]    = true,
        [PRINT_STACK_REGIONS]   = true,
        [PRINT_OTHER_REGIONS]   = true
};

#define set_each_print_option(__bool) do {              \
        for (uint __i = PRINT_USER_CONTEXT;             \
             __i <= PRINT_OTHER_REGIONS; __i++)         \
             print_options[__i] = (__bool);             \
} while (0)

#define set_each_region_option(__bool) do {             \
        for (uint __i = PRINT_TEXT_REGIONS;             \
             __i <= PRINT_OTHER_REGIONS; __i++)         \
             print_options[__i] = (__bool);             \
} while (0)

enum {
        REGION_START,
        REGION_END,
        REGION_SIZE,
        REGION_PROTECTION,
        REGION_SHARE_MODE,
        REGION_USER_TAG,
        REGION_INHERITANCE
};

static bool vm_info_options[] = {
        [REGION_START]          = true,
        [REGION_END]            = true,
        [REGION_SIZE]           = true,
        [REGION_PROTECTION]     = true,
        [REGION_SHARE_MODE]     = true,
        [REGION_USER_TAG]       = true,
        [REGION_INHERITANCE]    = true 
};

#define set_each_vm_info_option(__bool) do {                            \
        for (uint __i = REGION_START; __i <= REGION_INHERITANCE; __i++) \
                vm_info_options[__i] = (__bool);                        \
} while (0)

static const char *help =
"OVERVIEW: xnd_print\n\n"
"DESCRIPTION: Print serialized checkpoint image files\n\n"
"USAGE: ./xnd_print [options] file ...\n\n"
"OPTIONS:\n\n"
"  --all, -a\n"
"    Print all checkpoint image file contents (default).\n\n"
"  --regions=<type>\n"
"    Valid values for <type> are: all, heap, stack, text, and data.\n"
"    Multiple values for <type> can be included in one string\n"
"    where each type is delimeted by a comma.\n\n"
"  --region-info=<info>\n"
"    Valid values for <info>:\n"
"       all, start, end, size, prot, mode, tag, inherit.\n"
"    Multiples values for <info> can be included.\n\n"
"  --print-context=[true | false]\n"
"    Specify whether or not the print the user context included in the\n"
"    checkpoint image file. (default: true)\n";

static void parse_region_options(char *);
static void parse_region_info_options(char *);
static void usage(void);

static const char *vm_inherit_string(struct xnd_vm_region *);
static const char *vm_share_mode_string(struct xnd_vm_region *);
static const char *vm_user_tag_string(struct xnd_vm_region *);

static int read_vm_region(int, struct xnd_vm_region *);
static int read_ckpt(int, struct xnd_ckpt_header *,
                     enum xnd_ckpt_entry *,
                     struct xnd_vm_region *,
                     ucontext_t *);

static bool skip_vm_region(struct xnd_vm_region *);
static void print_ckpt_header(struct xnd_ckpt_header *);
static void print_vm_regions(struct xnd_vm_region *, u32);
static void print_user_context(ucontext_t *);
static void print_checkpoint(int);

int main(int argc, char *argv[])
{
        int     dirfd = -1, fd = -1;
        size_t  len;
        char    dir[PATH_MAX], ckpt[PATH_MAX], *str;
        bool    compressed = false;

        if (argc < 2) {
                usage();
                exit(0);
        }
        
        bzero(ckpt, sizeof(ckpt));
        argc--;
        do {
                argc--; argv++;
                if (ARG_IS_CKPT(argv[0])) {
                        strncpy(ckpt, argv[0], strlen(argv[0]) + 1);
                        if (ARG_IS_COMPRESSED_CKPT(ckpt)) {
                                compressed = true;
                                str = strstr(ckpt, XND_COMPRESSED_SUFFIX);
                                *str = '\0';
                        }
                        break;
                } else if (ARG_IS_ALL(argv[0])) {
                        set_each_print_option(true);
                        set_each_vm_info_option(true);
                } else if (ARG_IS_REGIONS(argv[0])) {
                        len = sizeof("--regions");
                        parse_region_options(argv[0] + len);
                } else if (ARG_IS_REGION_INFO(argv[0])) {
                        len = sizeof("--region-info");
                        parse_region_info_options(argv[0] + len);
                } else if (ARG_IS_PRINT_CONTEXT(argv[0])) {
                        len = sizeof("--print-context");
                        if (strstr(argv[0], "true"))
                                print_options[PRINT_USER_CONTEXT] = true;
                        else
                                print_options[PRINT_USER_CONTEXT] = false;
                } else {
                        xnd_error("Unrecognized argument: %s\n", argv[0]);
                        usage();
                        exit(XND_EXIT_SUCCESS);
                }
        } while (argc);

        if (compressed) {
		xnd_path_dirname(dir, sizeof(dir), ckpt);
                dirfd = open(dir, O_RDONLY | O_DIRECTORY);
                if (dirfd < 0) {
                        xnd_error("open(%s): %s\n", dir, strerror(errno));
                        goto bad;
                }
                if (xnd_decompress_ckpt(dirfd, ckpt) != 0) {
                        xnd_error("xnd_decompress_ckpt failed\n");
                        goto bad;
                }
        }

        xnd_assert(access(ckpt, F_OK) == 0);
        if ((fd = open(ckpt, O_RDONLY)) < 0) {
                xnd_error("open(%s): %s\n", ckpt, strerror(errno));
                goto bad;
        }

        print_checkpoint(fd);
        exit(XND_EXIT_SUCCESS);
bad:
        if (fd != -1)
                close(fd);
        if (dirfd != -1)
                close(dirfd);
        exit(XND_EXIT_FAILURE);
}

static const char *vm_inherit_string(struct xnd_vm_region *region)
{
        switch (region->inherit) {
        case VM_INHERIT_SHARE:
                return "VM_INHERIT_SHARE";
        case VM_INHERIT_COPY:
                return "VM_INHERIT_COPY";
        case VM_INHERIT_NONE:
                return "VM_INHERIT_NONE";
        case VM_INHERIT_DONATE_COPY:
                return "VM_INHERIT_DONATE_COPY";
        default:
                __builtin_trap();
        }
}

static const char *vm_share_mode_string(struct xnd_vm_region *region)
{
        switch (region->mode) {
        case SM_COW:
                return "SM_COW";
        case SM_PRIVATE:
                return "SM_PRIVATE";
        case SM_EMPTY:
                return "SM_EMPTY";
        case SM_SHARED:
                return "SM_SHARED";
        case SM_TRUESHARED:
                return "SM_TRUESHARED";
        case SM_PRIVATE_ALIASED:
                return "SM_PRIVATE_ALIASED";
        case SM_SHARED_ALIASED:
                return "SM_SHARED_ALIASED";
        case SM_LARGE_PAGE:
                return "SM_LARGE_PAGE";
        default:
                __builtin_trap();
        }
}

static const char *vm_user_tag_string(struct xnd_vm_region *region)
{
        switch (region->tag) {
        case VM_MEMORY_MALLOC:
                return "VM_MEMORY_MALLOC";
        case VM_MEMORY_MALLOC_SMALL:
                return "VM_MEMORY_MALLOC_SMALL";
        case VM_MEMORY_MALLOC_LARGE:
                return "VM_MEMORY_MALLOC_LARGE";
        case VM_MEMORY_MALLOC_HUGE:
                return "VM_MEMORY_MALLOC_HUGE";
        case VM_MEMORY_SBRK:
                return "VM_MEMORY_SBRK";
        case VM_MEMORY_REALLOC:
                return "VM_MEMORY_REALLOC";
        case VM_MEMORY_MALLOC_TINY:
                return "VM_MEMORY_MALLOC_TINY";
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
                return "VM_MEMORY_MALLOC_LARGE_REUSABLE";
        case VM_MEMORY_MALLOC_LARGE_REUSED:
                return "VM_MEMORY_MALLOC_LARGE_REUSED";
        case VM_MEMORY_MALLOC_NANO:
                return "VM_MEMORY_MALLOC_NANO";
        case VM_MEMORY_MALLOC_MEDIUM:
                return "VM_MEMORY_MALLOC_MEDIUM";
        case VM_MEMORY_MALLOC_PROB_GUARD:
                return "VM_MEMORY_MALLOC_PROB_GUARD";
        case VM_MEMORY_STACK:
                return "VM_MEMORY_STACK";
        case VM_MEMORY_GUARD:
                return "VM_MEMORY_GUARD";
        case VM_MEMORY_SHARED_PMAP:
                return "VM_MEMORY_SHARED_PMAP";
        case VM_MEMORY_DYLIB:
                return "VM_MEMORY_DYLIB";
        case VM_MEMORY_DYLD:
                return "VM_MEMORY_DYLD";
        case VM_MEMORY_DYLD_MALLOC:
                return "VM_MEMORY_DYLD_MALLOC";
        case VM_MEMORY_LIBDISPATCH:
                return "VM_MEMORY_LIBDISPATCH";
        case VM_MEMORY_RESTART_STACK:
                return "VM_MEMORY_RESTART_STACK";
        default:
                return "NIL";
        }
}

static int
read_vm_region(int fd, struct xnd_vm_region *region)
{
	int ret;
	off_t off;

	if (ONLY_RESTORE_DIRTY_PAGES(region) && region->dirty_only) {
		off = sizeof(struct xnd_vm_page) + VM_PAGE_SIZE;
		off *= region->pages_dirtied;
	} else {
		off = region->size;
	}

	ret = (lseek(fd, off, SEEK_CUR) == -1 ? -1 : 0);
	return ret;
}

static int
read_ckpt(int fd,
	  struct xnd_ckpt_header *header,
	  enum xnd_ckpt_entry *entries,
	  struct xnd_vm_region *regions,
          ucontext_t *uctx)
{
	int ret;
	ssize_t bread;
	struct xnd_vm_region *region = regions;

        for (u32 i = 0; i < header->entry_count; i++) {
                if (readall(fd, &entries[i], sizeof(entries[i])) < 0) {
                        xnd_error("Failed to read checkpoint entry!\n");
                        return -1;
                }

                switch (entries[i]) {
                case XND_VM_REGION_ENTRY: {
			bread = readall(fd, region, sizeof(*region));
			if (bread != sizeof(*region)) {
				xnd_error("failed to read vm region\n");
				goto bad;
			}

			ret = read_vm_region(fd, region);
			if (ret != 0) {
				xnd_error("failed to read vm region\n");
				goto bad;
			}

                        region++;
                        break;
                }
                case XND_UCONTEXT_ENTRY: {
			ret = readall(fd, uctx, sizeof(*uctx));
			if (ret != sizeof(*uctx)) {
				xnd_error("Failed to read ucontext\n");
				goto bad;
			}
                        break;
                }
                default:
                        xnd_abort();
                }
        }

        close(fd);
        return 0;
bad:
        close(fd);
        return -1;
}

static bool skip_vm_region(struct xnd_vm_region *region)
{
        if (region->prot == VM_PROT_NONE) {
                if (print_options[PRINT_OTHER_REGIONS])
                        return false;
                return true;
        }

        switch (region->tag) {
        case VM_MEMORY_MALLOC:
        case VM_MEMORY_MALLOC_SMALL:
        case VM_MEMORY_MALLOC_LARGE:
        case VM_MEMORY_MALLOC_HUGE:
        case VM_MEMORY_MALLOC_TINY:
        case VM_MEMORY_MALLOC_LARGE_REUSABLE:
        case VM_MEMORY_MALLOC_LARGE_REUSED:
        case VM_MEMORY_MALLOC_NANO:
        case VM_MEMORY_MALLOC_MEDIUM:
        case VM_MEMORY_MALLOC_PROB_GUARD:
                if (print_options[PRINT_HEAP_REGIONS])
                        return false;
                return true;
        case VM_MEMORY_STACK:
                if (print_options[PRINT_STACK_REGIONS])
                        return false;
                return true;
        case VM_MEMORY_SBRK:
        case VM_MEMORY_REALLOC:
        case VM_MEMORY_GUARD:
        case VM_MEMORY_SHARED_PMAP:
        case VM_MEMORY_DYLIB:
        case VM_MEMORY_DYLD:
        case VM_MEMORY_DYLD_MALLOC:
        case VM_MEMORY_LIBDISPATCH:
                if (print_options[PRINT_OTHER_REGIONS])
                        return false;
                return true;
        default:
                break;
        }

        if (region->mode == SM_EMPTY) {
                if (print_options[PRINT_OTHER_REGIONS])
                        return false;
                return true;
        }

        if (region->prot == (VM_PROT_EXECUTE | VM_PROT_READ)) {
                if (print_options[PRINT_TEXT_REGIONS])
                        return false;
                return true;
        }

        if (region->mode == SM_COW || region->mode == SM_PRIVATE) {
                if (print_options[PRINT_DATA_REGIONS])
                        return false;
                return true;
        }

        if (print_options[PRINT_OTHER_REGIONS])
                return false;

        return true;
}

static void
print_ckpt_header(struct xnd_ckpt_header *header)
{
	struct shared_cache_info *dyld_cache_info;
	char dyld_cache_uuid[37];
	uintptr_t dyld_cache_base, dyld_cache_end;
	size_t dyld_cache_size;
	const char *root_of_tree = NULL;

	dyld_cache_info = &header->shared_cache_info;
	uuid_unparse(dyld_cache_info->uuid, dyld_cache_uuid);

	dyld_cache_base = (uintptr_t)dyld_cache_info->base;
	dyld_cache_size = dyld_cache_info->size;
	dyld_cache_end = dyld_cache_base + dyld_cache_size;

	root_of_tree = (header->is_root_of_tree ? "true" : "false");

	printf("*************** Checkpoint Header ***************\n");
        printf("                   Magic: %s\n"
               "                 xnd_pid: %u\n"
               "                xnd_ppid: %u\n"
               "                xnd_pgid: %u\n"
               "                     pid: %d\n"
               "                    ppid: %d\n"
               "                    pgid: %d\n"
               "                     sid: %d\n"
               "          mach_task_self: %u\n"
               "          mach_host_self: %u\n"
               "              # of peers: %u\n"
               "   Root of process tree?: %s\n"
               "            # of entries: %u\n"
               "         # of vm regions: %u\n"
               " dyld shared cache start: 0x%016lx\n"
               "   dyld shared cache end: 0x%016lx\n"
               "  dyld shared cache size: %zu\n"
               "  dyld shared cache uuid: %s\n",
	       header->magic, header->xnd_pid, header->xnd_ppid,
	       header->xnd_pgid, header->pid, header->ppid, header->pgid,
	       header->sid, header->task_self, header->host_self,
	       header->num_peers, root_of_tree, header->entry_count,
	       header->region_count, dyld_cache_base, dyld_cache_end,
	       dyld_cache_size, dyld_cache_uuid);
	printf("*************************************************\n");
}

static void
print_vm_regions(struct xnd_vm_region *regions, u32 nregions)
{
	size_t tmp, bytes = 0;
	struct xnd_vm_region *it;
	const struct xnd_vm_region *end = regions + nregions;

	for (it = regions; it != end; it++) {
		if (ONLY_RESTORE_DIRTY_PAGES(it) && it->dirty_only) {
			tmp = sizeof(struct xnd_vm_page) + VM_PAGE_SIZE;
			tmp *= it->pages_dirtied;
		} else {
			tmp = it->size;
		}
		tmp += sizeof(struct xnd_vm_region);
		bytes += tmp;
	}

	printf("********** Checkpointed Memory Regions **********\n");
	printf("Total checkpointed memory: %.2fGB/%.2fMB/%zu bytes\n\n",
	       GIGABYTES(bytes), MEGABYTES(bytes), bytes);

	for (it = regions; it != end; it++) {
		if (skip_vm_region(it))
			continue;
		if (ONLY_RESTORE_DIRTY_PAGES(it) && it->dirty_only) {
			tmp = sizeof(struct xnd_vm_page) + VM_PAGE_SIZE;
			tmp *= it->pages_dirtied;
		} else {
			tmp = it->size;
		}
		tmp += sizeof(struct xnd_vm_region);
		printf("\tMemory region #%u\n", (u32)(it - regions));
		printf("\t       start=%p\n"
		       "\t         end=%p\n"
		       "\t        size=%zu\n"
		       "\t        prot=%s/%s\n"
		       "\t        mode=%s\n"
		       "\t         tag=%s\n"
		       "\t     inherit=%s\n"
		       "\t dirty pages=%u\n"
		       "\t total bytes=%zu (in checkpoint image)\n\n",
		       it->start,
		       (void *)((uintptr_t)it->start + it->size),
		       it->size, VM_PROT_STRING(it->prot),
		       VM_PROT_STRING(it->max_prot),
		       vm_share_mode_string(it), vm_user_tag_string(it),
		       vm_inherit_string(it), it->pages_dirtied, bytes);
	}
	printf("*************************************************\n");
}

static void print_user_context(ucontext_t *uctx)
{
        mcontext_t mctx = (mcontext_t)&uctx->__mcontext_data;


	printf("*********** Checkpointed User Context ***********\n");
        /* Callee-saved general purpose registers */
        for (u32 i = 19; i <= 28; i++)
                printf("\tx%u:\t0x%llx\n", i, mctx->__ss.__x[i]);

        /* FP/Vector registers */
        for (u32 i = 8; i <= 15; i++)
                printf("\td%u:\t0x%llx\n", i, (u64)mctx->__ns.__v[i]);

        printf("\tfp:\t0x%llx\n", get_mcontext_fp(mctx));
        printf("\tlr:\t0x%llx\n", get_mcontext_lr(mctx));
        printf("\tsp:\t0x%llx\n", get_mcontext_sp(mctx));
	printf("*************************************************\n");
}

static void print_checkpoint(int fd)
{
        struct xnd_ckpt_header header;

        if (readall(fd, &header, sizeof(header)) < 0) {
                exit(EXIT_FAILURE);
        }

        print_ckpt_header(&header);

        enum xnd_ckpt_entry     entries[header.entry_count];
        struct xnd_vm_region    regions[header.region_count];
        ucontext_t              uctx;

        if (read_ckpt(fd, &header, entries, regions, &uctx) < 0) {
                exit(EXIT_FAILURE);
        }

        print_vm_regions(regions, header.region_count);
        if (print_options[PRINT_USER_CONTEXT]) {
                print_user_context(&uctx);
        }
}

static void usage(void)
{
        xnd_printf("%s", help);
}

static void parse_region_options(char *options)
{
        if (strstr(options, "all")) {
                set_each_region_option(true);
                return;
        } else {
                set_each_region_option(false);
        }

        if (strstr(options, "heap"))
                print_options[PRINT_HEAP_REGIONS] = true;
        if (strstr(options, "stack"))
                print_options[PRINT_STACK_REGIONS] = true;
        if (strstr(options, "data"))
                print_options[PRINT_DATA_REGIONS] = true;
        if (strstr(options, "text"))
                print_options[PRINT_TEXT_REGIONS] = true;
}

static void parse_region_info_options(char *options)
{
        if (strstr(options, "all")) {
                set_each_vm_info_option(true);
                return;
        } else {
                set_each_vm_info_option(false);
        }

        if (strstr(options, "start"))
                vm_info_options[REGION_START] = true;
        if (strstr(options, "end"))
                vm_info_options[REGION_END] = true;
        if (strstr(options, "size"))
                vm_info_options[REGION_SIZE] = true;
        if (strstr(options, "prot"))
                vm_info_options[REGION_PROTECTION] = true;
        if (strstr(options, "mode"))
                vm_info_options[REGION_SHARE_MODE] = true;
        if (strstr(options, "inherit"))
                vm_info_options[REGION_INHERITANCE] = true;
}
