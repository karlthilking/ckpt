/* xnd_print.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <assert.h>
#include <fcntl.h>
#include <ucontext.h>
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/pac.h"
#include "xnd/vm_region.h"

#define KILOBYTES(__bytes) \
        (((float)(__bytes)) / 1024.0f)
#define MEGABYTES(__bytes) \
        (KILOBYTES((__bytes)) / 1024.0f)
#define GIGABYTES(__bytes) \
        (MEGABYTES((__bytes)) / 1024.0f)

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
"DESCRIPTION: Print serialized checkpoint image files (.ckpt)\n\n"
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
static const char *vm_inherit_string(const struct xnd_vm_region *);
static const char *vm_share_mode_string(const struct xnd_vm_region *);
static const char *vm_user_tag_string(const struct xnd_vm_region *);
static int readall(int, void *, size_t);
static int read_ckpt(int, const struct xnd_ckpt_header *,
                     enum xnd_ckpt_entry *,
                     struct xnd_vm_region *,
                     ucontext_t *);
static bool skip_vm_region(const struct xnd_vm_region *);
static void print_vm_regions(const struct xnd_vm_region *, u32);
static void print_user_context(ucontext_t *);
static void print_checkpoint(int);

int main(int argc, char *argv[])
{
        int     fd;
        size_t  len;

        if (argc < 2) {
                usage();
                exit(0);
        }
        
        argc--;
        do {
                argc--; argv++;
                if (strstr(argv[0], ".ckpt"))
                        break;

                if (!strcmp(argv[0], "-a") || !strcmp(argv[0], "--all")) {
                        set_each_print_option(true);
                        set_each_vm_info_option(true);
                        break;
                }
                
                len = strlen("--regions");
                if (!strncmp(argv[0], "--regions", len))
                        parse_region_options(argv[0] + len);
                
                len = strlen("--region-info");
                if (!strncmp(argv[0], "--region-info", len))
                        parse_region_info_options(argv[0] + len);
                
                len = strlen("--print-context");
                if (!strncmp(argv[0], "--print-context", len)) {
                        if (strstr(argv[0], "true"))
                                print_options[PRINT_USER_CONTEXT] = true;
                        else
                                print_options[PRINT_USER_CONTEXT] = false;
                }
        } while (argc);

        fd = open(argv[0], O_RDONLY);
        if (fd < 0)
                err(EXIT_FAILURE, "open(%s)", argv[0]);
                                
        print_checkpoint(fd);
        exit(0);
}

static const char *vm_inherit_string(const struct xnd_vm_region *region)
{
        static_assert(VM_INHERIT_COPY == VM_INHERIT_DEFAULT &&
                      VM_INHERIT_LAST_VALID == VM_INHERIT_NONE, "");

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

static const char *vm_share_mode_string(const struct xnd_vm_region *region)
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

static const char *vm_user_tag_string(const struct xnd_vm_region *region)
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

static int readall(int fd, void *buf, size_t size)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < size; bytes += retval) {
                retval = read(fd, buf + bytes, size - bytes);
                if (retval < 0) {
                        perror("read");
                        return -1;
                }
        }
        
        return 0;
}

static int read_ckpt(int fd, const struct xnd_ckpt_header *header,
                     enum xnd_ckpt_entry *entries,
                     struct xnd_vm_region *regions,
                     ucontext_t *uctx)
{
        struct xnd_vm_region    *rgn = regions;
        int                     retval;

        for (u32 i = 0; i < header->entry_count; i++) {
                if (readall(fd, &entries[i], sizeof(entries[i])) < 0) {
                        xnd_error("Failed to read checkpoint entry!\n");
                        return -1;
                }

                switch (entries[i]) {
                case XND_VM_REGION_ENTRY:
                        retval = readall(fd, rgn, sizeof(*rgn));
                        assert(lseek(fd, rgn->size, SEEK_CUR) != -1);
                        rgn++;
                        break;
                case XND_UCONTEXT_ENTRY:
                        retval = readall(fd, uctx, sizeof(ucontext_t));
                        break;
                default:
                        xnd_abort();
                }

                if (retval < 0) {
                        xnd_error("Failed to read checkpoint data!\n");
                        goto bad;
                }
        }

        close(fd);
        return 0;
bad:
        close(fd);
        return -1;
}

static bool skip_vm_region(const struct xnd_vm_region *region)
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

static void print_vm_regions(const struct xnd_vm_region *regions, u32 nr_rgns)
{
        const struct xnd_vm_region      *rgn;
        size_t                          nbyte = 0;

        for (rgn = regions; rgn < regions + nr_rgns; rgn++)
                nbyte += rgn->size;

        printf("************* Checkpointed Memory Regions *************\n");
        printf("Total memory in checkpoint: %.2fGB/%.2fMB/%zu bytes\n\n",
               GIGABYTES(nbyte), MEGABYTES(nbyte), nbyte);
                
        for (rgn = regions; rgn < regions + nr_rgns; rgn++) {
                if (skip_vm_region(rgn))
                        continue;
                printf("\tMemory Region #%d:\n", (int)(rgn - regions));
                if (vm_info_options[REGION_START])
                        printf("\t   start=%p\n", rgn->start);
                if (vm_info_options[REGION_END])
                        printf("\t     end=%p\n", rgn->end);
                if (vm_info_options[REGION_SIZE])
                        printf("\t    size=%zu\n", rgn->size);
                if (vm_info_options[REGION_PROTECTION])
                        printf("\t    prot=%s/%s\n",
                               VM_PROT_STRING(rgn->prot),
                               VM_PROT_STRING(rgn->max_prot));
                if (vm_info_options[REGION_SHARE_MODE])
                        printf("\t    mode=%s\n", vm_share_mode_string(rgn));
                if (vm_info_options[REGION_USER_TAG])
                        printf("\t     tag=%s\n", vm_user_tag_string(rgn));
                if (vm_info_options[REGION_INHERITANCE])
                        printf("\t inherit=%s\n", vm_inherit_string(rgn));
                printf("\n");
        }
        printf("*******************************************************\n");
}

static void print_user_context(ucontext_t *uctx)
{
        mcontext_t mctx = (mcontext_t)&uctx->__mcontext_data;

        printf("************** CHECKPOINTED USER CONTEXT **************\n");
        /* Callee-saved general purpose registers */
        for (u32 i = 19; i <= 28; i++)
                printf("\tx%u:\t0x%llx\n", i, mctx->__ss.__x[i]);

        /* FP/Vector registers */
        for (u32 i = 8; i <= 15; i++)
                printf("\td%u:\t0x%llx\n", i, (u64)mctx->__ns.__v[i]);

        printf("\tfp:\t0x%llx\n", get_mcontext_fp(mctx));
        printf("\tlr:\t0x%llx\n", get_mcontext_lr(mctx));
        printf("\tsp:\t0x%llx\n", get_mcontext_sp(mctx));
        printf("*******************************************************\n");
}

static void print_checkpoint(int fd)
{
        struct xnd_ckpt_header header;

        if (readall(fd, &header, sizeof(header)) < 0)
                exit(EXIT_FAILURE);

        enum xnd_ckpt_entry     entries[header.entry_count];
        struct xnd_vm_region    regions[header.region_count];
        ucontext_t              uctx;
        
        if (read_ckpt(fd, &header, entries, regions, &uctx) < 0)
                exit(EXIT_FAILURE);

        print_vm_regions(regions, header.region_count);
        if (print_options[PRINT_USER_CONTEXT])
                print_user_context(&uctx);
}

static void usage(void)
{
        fprintf(stderr, "%s", help);
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
