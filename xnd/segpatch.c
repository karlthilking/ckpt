/* segpatch.c */
#include "xnd/xnd.h"
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_prot.h>
#include <mach-o/loader.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAGE_ALIGNED(x) (!((x) & (VM_PAGE_SIZE - 1)))

#define ARG_CREATE(arg) \
        (strncmp((arg), "-c", sizeof("-c")) == 0 || \
         strncmp((arg), "--create", sizeof("--create")) == 0)
#define ARG_EDIT(arg) \
        (strncmp((arg), "-e", sizeof("-e")) == 0 || \
         strncmp((arg), "--edit", sizeof("--edit")) == 0)
#define ARG_HELP(arg) \
        (strncmp((arg), "-h", sizeof("-h")) == 0 || \
         strncmp((arg), "--help", sizeof("--help")) == 0)
#define ARG_VERBOSE(arg) \
        (strncmp((arg), "-v", sizeof("-v")) == 0 || \
         strncmp((arg), "--verbose", sizeof("--verbose")) == 0)
#define ARG_PROT(arg) \
        (strncmp((arg), "--prot", sizeof("--prot")) == 0)
#define ARG_VMADDR(arg) \
        (strncmp((arg), "--vmaddr", sizeof("--vmaddr")) == 0)
#define ARG_VMSIZE(arg) \
        (strncmp((arg), "--vmsize", sizeof("--vmsize")) == 0)
#define ARG_SEGNAME(arg) \
        (strncmp((arg), "--segname", sizeof("--segname")) == 0)
#define ARG_SECTNAME(arg) \
        (strncmp((arg), "--sectname", sizeof("--sectname")) == 0)

static const char *help =
"OVERVIEW: Patch a new __SEGMENT,__section pair into executable\n\n"
"USAGE: ./segpatch [options] file...\n\n"
"OPTIONS:\n"
" -c, --create\n"
"  Create a new segment (default: true)\n"
"  The parameters of the new segment should be specified\n"
"  with --vmaddr, --vmsize, --segname, and --sectname.\n"
" -e, --edit\n"
"  Edit an exiting segment in the executable (default: false)\n"
" --prot <init>/<max>\n"
"  Specify the initial and maximum protection for the segment\n"
" --vmaddr <address>\n"
"  Specify start address for the new segment (default: ---/rwx)\n"
" --vmsize <size>\n"
"  Specify virtual memory size for the new segment\n"
" --segname <name>\n"
"  Name to give the new segment. The given name should be\n"
"  of the form __NAME (leading underscores, uppercase)\n"
" --sectname <name>\n"
"  Name for the new segment's only section. The given name\n"
"  should be of the form __name (leading underscores, lowercase)\n"
" -h, --help\n"
"  Display help message\n"
" -v, --verbose\n"
"  Print details about the patched executable\n\n";

static void print_segment_command(struct segment_command_64 *);
static void print_section(struct section_64 *);
static void usage_and_exit(int);
static int segcreate(void);
static int segedit(void);
static int set_seg_prot(const char *);

static u8 *image = NULL;
static u64 vmaddr = 0, vmsize = 0;
static vm_prot_t initprot = 0, maxprot = VM_PROT_ALL;
static const char *segname = NULL, *sectname = NULL;
static const char *filename = NULL;
static bool verbose = false, edit = false, create = true;

int main(int argc, char *argv[])
{
        int fd, ret;
        struct stat st = {0};

        if (argc < 2) {
                usage_and_exit(-1);
        }

        argv++;
        argc--;
        do {
                if (ARG_HELP(argv[0])) {
                        usage_and_exit(0);
                } else if (ARG_CREATE(argv[0])) {
                        create = true;
                        edit = false;
                        argv++;
                        argc--;
                } else if (ARG_EDIT(argv[0])) {
                        create = false;
                        edit = true;
                        argv++;
                        argc--;
                } else if (ARG_VERBOSE(argv[0])) {
                        verbose = true;
                        argv++;
                        argc--;
                } else if (ARG_PROT(argv[0])) {
                        if (set_seg_prot(argv[1]) < 0) {
                                printf("Bad vm prot: %s\n", argv[1]);
                                exit(-1);
                        }
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_VMADDR(argv[0])) {
                        vmaddr = (strncmp(argv[1], "0x", 2) == 0 ?
                                  strtoull(argv[1], NULL, 16) :
                                  strtoull(argv[1], NULL, 10));
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_VMSIZE(argv[0])) {
                        vmsize = (strncmp(argv[1], "0x", 2) == 0 ?
                                  strtoull(argv[1], NULL, 16) :
                                  strtoull(argv[1], NULL, 10));
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_SEGNAME(argv[0])) {
                        segname = argv[1];
                        argv++; argv++;
                        argc--; argc--;
                } else if (ARG_SECTNAME(argv[0])) {
                        sectname = argv[1];
                        argv++; argv++;
                        argc--; argc--;
                } else if (strstr(argv[0], "xnd_restart_internal")) {
                        filename = argv[0];
                        if (access(filename, X_OK) != 0) {
                                printf("Bad executable: %s\n", filename);
                                exit(-1);
                        }
                        argv++;
                        argc--;
                } else {
                        printf("Bad option: %s\n", argv[0]);
                        usage_and_exit(-1);
                }
        } while (argc);

        if (vmaddr == 0) {
                printf("--vmaddr not specified\n");
                usage_and_exit(-1);
        } else if (vmsize == 0) {
                printf("--vmsize not specified\n");
                usage_and_exit(-1);
        } else if (segname == NULL) {
                printf("--segname not specified\n");
                usage_and_exit(-1);
        } else if (sectname == NULL) {
                printf("--segname not specified\n");
                usage_and_exit(-1);
        } else if (filename == NULL) {
                printf("No input file provided\n");
                usage_and_exit(-1);
        }

        if ((fd = open(filename, O_RDWR)) < 0) {
                perror("open");
                exit(-1);
        }

        if (fstat(fd, &st) < 0) {
                perror("fstat");
                close(fd);
                exit(-1);
        }

        image = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED , fd, 0);
        if ((void *)image == MAP_FAILED) {
                perror("mmap");
                close(fd);
                exit(-1);
        }

        if (create && (ret = segcreate()) < 0)
                printf("Failed to create new segment\n");
        else if (edit && (ret = segedit()) < 0)
                printf("Failed to edit segment\n");

        if (ret < 0) {
                printf("Failed to patch executable: %s\n", filename);
                close(fd);
                munmap(image, st.st_size);
                exit(-1);
        }

        msync(image, st.st_size, MS_SYNC);
        munmap(image, st.st_size);
        close(fd);
        exit(0);
}

static void print_segment_command(struct segment_command_64 *seg)
{
        const char *vm_prot_str[] = {
                [VM_PROT_NONE] = "---",
                [VM_PROT_READ] = "r--",
                [VM_PROT_WRITE] = "-w-",
                [VM_PROT_EXECUTE] = "--x",
                [VM_PROT_DEFAULT] = "rw-",
                [VM_PROT_READ | VM_PROT_EXECUTE] = "r-x",
                [VM_PROT_WRITE | VM_PROT_EXECUTE] = "-wx",
                [VM_PROT_ALL] = "rwx",
        };

        printf("Segment\n"
               "      cmd LC_SEGMENT_64\n"
               "  cmdsize %u\n"
               "  segname %s\n"
               "   vmaddr %016llx\n"
               "   vmsize %016llx\n"
               "  fileoff %llu\n"
               " filesize %llu\n"
               "  maxprot %s\n"
               " initprot %s\n"
               "   nsects %u\n"
               "    flags (none)\n",
               seg->cmdsize, seg->segname, seg->vmaddr, seg->vmsize,
               seg->fileoff, seg->filesize, vm_prot_str[seg->maxprot],
               vm_prot_str[seg->initprot], seg->nsects);
}

static void print_section(struct section_64 *sect)
{
        printf("Section"
               "  sectname %s\n"
               "   segname %s\n"
               "      addr 0x%016llx\n"
               "      size 0x%016llx\n"
               "    offset %u\n"
               "     align 2^%u (%u)\n"
               "    reloff %u\n"
               "    nreloc %u\n"
               "      type S_ZEROFILL\n",
               sect->sectname, sect->segname, sect->addr, sect->size,
               sect->offset, sect->align, 1u << sect->align,
               sect->reloff, sect->nreloc);
}

static void usage_and_exit(int code)
{
        printf("%s", help);
        exit(code);
}

static int segcreate(void)
{
        struct mach_header_64 *mh;
        u8 *p;

        mh = (struct mach_header_64 *)image;
        if (mh->magic != MH_MAGIC_64 || mh->filetype != MH_EXECUTE) {
                printf("Bad mach-o executable: %s\n", filename);
                return -1;
        }

        struct segment_command_64 seg = {
                .cmd = LC_SEGMENT_64,
                .cmdsize = 0,
                .segname = {0},
                .vmaddr = vmaddr,
                .vmsize = vmsize,
                .filesize = 0,
                .fileoff = 0,
                .maxprot = maxprot,
                .initprot = initprot,
                .nsects = 1,
                .flags = 0
        };

        struct section_64 sect = {
                .sectname = {0},
                .segname = {0},
                .addr = vmaddr,
                .size = vmsize,
                .offset = 0,
                .align = 14,
                .reloff = 0,
                .nreloc = 0,
                .flags = S_ZEROFILL,
                .reserved1 = 0,
                .reserved2 = 0,
                .reserved3 = 0,
        };

        seg.cmdsize = sizeof(seg) + sizeof(sect);
        strncpy(seg.segname, segname, sizeof(seg.segname));
        strncpy(sect.sectname, sectname, sizeof(sect.sectname));
        strncpy(sect.segname, segname, sizeof(sect.segname));

        p = (u8 *)mh + sizeof(*mh) + mh->sizeofcmds;
        memcpy(p, &seg, sizeof(seg));
        memcpy(p + sizeof(seg), &sect, sizeof(sect));

        mh->ncmds += 1;
        mh->sizeofcmds += seg.cmdsize;

        if (verbose) {
                printf("Patched in segment in %s:\n", filename);
                print_segment_command(&seg);
                print_section(&sect);
        }

        return 0;
}

static int segedit(void)
{
        struct load_command *lc;
        struct segment_command_64 *seg;
        struct section_64 *sect;
        struct mach_header_64 *mh = (struct mach_header_64 *)image;
        u32 cmdsize;
        u64 lcstart, offset;
        bool found = false;

        lcstart = (u64)mh + sizeof(*mh);
        for (offset = 0; offset < mh->sizeofcmds; offset += cmdsize) {
                lc = (struct load_command *)(lcstart + offset);
                cmdsize = lc->cmdsize;
                if (lc->cmd != LC_SEGMENT_64)
                        continue;

                seg = (struct segment_command_64 *)lc;
                if (strncmp(seg->segname, segname, 16) == 0) {
                        found = true;
                        break;
                }
        }

        if (!found) {
                printf("Failed to find %s,%s in load commands\n",
                       segname, sectname);
                return -1;
        }

        sect = (struct section_64 *)((u8 *)seg + sizeof(*seg));
        seg->vmaddr = vmaddr;
        seg->vmsize = vmsize;
        seg->maxprot = maxprot;
        seg->initprot = initprot;
        sect->addr = vmaddr;
        sect->size = vmsize;

        if (verbose) {
                printf("Updated segment in %s:\n", filename);
                print_segment_command(seg);
                print_section(sect);
        }

        return 0;
}

static int set_seg_prot(const char *prot_str)
{
        if (strlen(prot_str) != 7 || prot_str[3] != '/')
                return -1;

        initprot = ((prot_str[0] == 'r' ? VM_PROT_READ : 0) |
                    (prot_str[1] == 'w' ? VM_PROT_WRITE : 0) |
                    (prot_str[2] == 'x' ? VM_PROT_EXECUTE : 0));
        
        maxprot = ((prot_str[4] == 'r' ? VM_PROT_READ : 0) |
                   (prot_str[5] == 'w' ? VM_PROT_WRITE : 0) |
                   (prot_str[6] == 'x' ? VM_PROT_EXECUTE : 0));

        return 0;
}
